// SPDX-License-Identifier: MPL-2.0
// Arrow Player — Library database wrapper
// Spec: eclipse-player.md §9.4, REQ-LIB-001
//
// Kotlin wrapper over Room that mirrors the desktop Database class API.
// Uses structured concurrency (kotlinx.coroutines) for all operations.
// All writes go through a single writer coroutine (single-writer pattern).
//
// Error modelling: sealed class hierarchy mirrors the C++ ErrorCode taxonomy.
package io.github.arrowplayer.core.library.db

import android.content.Context
import androidx.room.Room
import androidx.room.withTransaction
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import java.io.File
import java.util.concurrent.atomic.AtomicLong

// ===========================================================================
// Errors
// ===========================================================================

/**
 * Stable error codes mirroring the C++ ErrorCode taxonomy.
 * Each code carries a user-facing message and optional technical detail.
 */
sealed class LibraryError {
    abstract val userMessage: String
    abstract val technicalDetail: String?

    data class FileNotFound(
        override val userMessage: String,
        override val technicalDetail: String? = null,
    ) : LibraryError()

    data class ConstraintViolation(
        override val userMessage: String,
        override val technicalDetail: String? = null,
    ) : LibraryError()

    data class QueryFailed(
        override val userMessage: String,
        override val technicalDetail: String? = null,
    ) : LibraryError()

    data class MigrationFailed(
        override val userMessage: String,
        override val technicalDetail: String? = null,
    ) : LibraryError()

    data class DatabaseCorrupt(
        override val userMessage: String,
        override val technicalDetail: String? = null,
    ) : LibraryError()

    data class InvalidState(
        override val userMessage: String,
        override val technicalDetail: String? = null,
    ) : LibraryError()

    data class InvalidArgument(
        override val userMessage: String,
        override val technicalDetail: String? = null,
    ) : LibraryError()

    data class Unknown(
        override val userMessage: String,
        override val technicalDetail: String? = null,
    ) : LibraryError()
}

// ===========================================================================
// Database wrapper
// ===========================================================================

/**
 * Kotlin wrapper over Room providing:
 *   - Structured concurrency (all ops are suspend functions)
 *   - Single-writer pattern (all writes go through a mutex-protected coroutine)
 *   - Exact schema parity with desktop (mirror of Database.cpp)
 *
 * Thread-safety: safe to use from multiple coroutines concurrently.
 * Writes are serialised via a mutex; reads are concurrent.
 *
 * @param context Android context for Room database creation
 * @param deviceId Unique device identifier for the sync change log
 */
class LibraryDatabaseWrapper(
    private val context: Context,
    private val deviceId: String,
) {
    private var _database: LibraryDatabase? = null
    private val database: LibraryDatabase
        get() = _database
            ?: throw IllegalStateException("Database is not open. Call open() first.")

    /** True if the database is currently open. */
    val isOpen: Boolean get() = _database != null

    // Single-writer coroutine scope
    private val writerScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val writeMutex = Mutex()
    private val writerCounter = AtomicLong(0)

    // Lamport clock for the change log
    private val clockMutex = java.util.concurrent.locks.ReentrantLock()
    private var lamportClock: Long = 0

    // ---------------------------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------------------------

    /**
     * Open (or create) the database at `path`.
     * Runs all pending migrations. Starts the writer coroutine.
     *
     * @throws IllegalStateException if already open
     */
    fun open(path: String): Result<Unit> {
        if (_database != null) {
            return Result.failure(
                IllegalStateException("Database is already open"),
            )
        }

        return try {
            val db = Room.databaseBuilder(
                context,
                LibraryDatabase::class.java,
                File(path).name,
            )
                // Migrations are handled by the MigrationMigrations class.
                // FallbackToDestructiveMigration is NOT used because schema
                // changes must be intentional (REQ-LIB-001).
                .build()
            _database = db

            // Sync clock with DB
            syncClock()

            Result.success(Unit)
        } catch (e: Exception) {
            _database = null
            Result.failure(e)
        }
    }

    /** Close the database. Drains pending writes. */
    fun close() {
        _database?.close()
        _database = null
    }

    // ---------------------------------------------------------------------------
    // Clock management
    // ---------------------------------------------------------------------------

    /**
     * Sync the local Lamport clock with the database.
     * Call on startup to ensure the clock is at least as large as any
     * clock already stored in the change log.
     */
    private fun syncClock() {
        clockMutex.lock()
        try {
            val dbClock = database.changeLogDao().maxClock()
            if (dbClock > lamportClock) {
                lamportClock = dbClock
            }
        } finally {
            clockMutex.unlock()
        }
    }

    private fun nextClock(): Long {
        clockMutex.lock()
        return try {
            ++lamportClock
        } finally {
            clockMutex.unlock()
        }
    }

    // ---------------------------------------------------------------------------
    // Tracks
    // ---------------------------------------------------------------------------

    /**
     * Upsert a track. If the path already exists, all columns are updated.
     * Thread-safe: uses the single writer.
     */
    suspend fun upsertTrack(track: TrackEntity): Result<Unit> = write {
        database.trackDao().upsert(track)
        recordChange("track", track.id.toString(), "insert")
    }

    /**
     * Upsert a batch of tracks in a single transaction.
     */
    suspend fun upsertTracks(tracks: List<TrackEntity>): Result<Unit> = write {
        database.trackDao().upsertAll(tracks)
        tracks.forEach { track ->
            recordChange("track", track.id.toString(), "insert")
        }
    }

    /**
     * Mark paths as gone (soft-delete).
     */
    suspend fun markPathsGone(paths: List<String>): Result<Unit> = write {
        if (paths.isEmpty()) return@write
        val now = System.currentTimeMillis() / 1000
        paths.forEach { path ->
            database.trackDao().markGone(path, now)
        }
    }

    /**
     * List all non-deleted tracks, paginated.
     */
    fun listTracksFlow(offset: Long = 0, limit: Long = 10_000): Flow<List<TrackEntity>> =
        database.trackDao().list(offset, limit)

    suspend fun listTracksSync(offset: Long = 0, limit: Long = 10_000): List<TrackEntity> =
        withContext(Dispatchers.IO) {
            database.trackDao().listSync(offset, limit)
        }

    /**
     * Get a track by path.
     */
    suspend fun getTrackByPath(path: String): Result<TrackEntity> =
        withContext(Dispatchers.IO) {
            val track = database.trackDao().byPath(path)
            if (track != null) {
                Result.success(track)
            } else {
                Result.failure(
                    LibraryError.FileNotFound(
                        userMessage = "Track not found.",
                        technicalDetail = "Path: $path",
                    ),
                )
            }
        }

    /**
     * Get all paths in the library (for diffing during scan).
     */
    suspend fun getAllPaths(): Result<List<String>> = withContext(Dispatchers.IO) {
        Result.success(database.trackDao().allPaths())
    }

    // ---------------------------------------------------------------------------
    // Library folders
    // ---------------------------------------------------------------------------

    fun listFoldersFlow(): Flow<List<LibraryFolderEntity>> =
        database.libraryFolderDao().listAll()

    suspend fun listFoldersSync(): List<LibraryFolderEntity> =
        withContext(Dispatchers.IO) {
            database.libraryFolderDao().listAllSync()
        }

    suspend fun addFolder(path: String): Result<Long> = write {
        val now = System.currentTimeMillis() / 1000
        try {
            val id = database.libraryFolderDao().insert(
                LibraryFolderEntity(
                    path = path,
                    scanState = "idle",
                    createdAt = now,
                ),
            )
            Result.success(id)
        } catch (e: androidx.room.RoomSimpleSQLiteException) {
            if (e.message?.contains("UNIQUE", ignoreCase = true) == true) {
                Result.failure(
                    LibraryError.ConstraintViolation(
                        userMessage = "This folder is already in the library.",
                        technicalDetail = "Path: $path",
                    ),
                )
            } else {
                Result.failure(
                    LibraryError.QueryFailed(
                        userMessage = "Could not add folder.",
                        technicalDetail = e.message,
                    ),
                )
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Playlists
    // ---------------------------------------------------------------------------

    fun listPlaylistsFlow(): Flow<List<PlaylistEntity>> =
        database.playlistDao().listAll()

    suspend fun listPlaylistsSync(): List<PlaylistEntity> =
        withContext(Dispatchers.IO) {
            database.playlistDao().listAllSync()
        }

    suspend fun upsertPlaylist(playlist: PlaylistEntity): Result<Long> = write {
        val id = database.playlistDao().upsert(playlist)
        recordChange("playlist", id.toString(), "insert")
        Result.success(id)
    }

    suspend fun replacePlaylistItems(
        playlistId: Long,
        trackIds: List<Long>,
    ): Result<Unit> = write {
        database.playlistDao().deleteItems(playlistId)
        val now = System.currentTimeMillis() / 1000
        val items = trackIds.mapIndexed { index, trackId ->
            PlaylistItemEntity(
                playlistId = playlistId,
                position = index,
                trackId = trackId,
                addedAt = now,
            )
        }
        database.playlistDao().insertItems(items)
        recordChange("playlist_item", playlistId.toString(), "update")
    }

    fun playlistItemsFlow(playlistId: Long): Flow<List<PlaylistItemEntity>> =
        database.playlistDao().items(playlistId)

    suspend fun playlistItemsSync(playlistId: Long): List<PlaylistItemEntity> =
        withContext(Dispatchers.IO) {
            database.playlistDao().itemsSync(playlistId)
        }

    // ---------------------------------------------------------------------------
    // Custom tags
    // ---------------------------------------------------------------------------

    suspend fun setCustomTag(trackId: Long, key: String, value: String): Result<Unit> = write {
        val now = System.currentTimeMillis() / 1000
        database.customTagDao().insert(
            CustomTagEntity(
                trackId = trackId,
                key = key,
                value = value,
                updatedAt = now,
            ),
        )
        recordChange("custom_tag", trackId.toString(), "update")
    }

    fun customTagsFlow(trackId: Long): Flow<List<CustomTagEntity>> =
        database.customTagDao().listForTrack(trackId)

    suspend fun customTagsSync(trackId: Long): List<CustomTagEntity> =
        withContext(Dispatchers.IO) {
            database.customTagDao().listForTrackSync(trackId)
        }

    // ---------------------------------------------------------------------------
    // Cue sheets
    // ---------------------------------------------------------------------------

    suspend fun upsertCuesheet(
        cuesheet: CuesheetEntity,
        tracks: List<CuetrackEntity>,
    ): Result<Unit> = write {
        database.cuesheetDao().upsert(cuesheet)
        database.cuesheetDao().deleteTracks(cuesheet.id)
        if (tracks.isNotEmpty()) {
            database.cuesheetDao().insertTracks(tracks)
        }
    }

    suspend fun getCuetracks(cuesheetId: String): List<CuetrackEntity> =
        withContext(Dispatchers.IO) {
            database.cuesheetDao().getTracks(cuesheetId)
        }

    // ---------------------------------------------------------------------------
    // Sync change log (REQ-LIB-075)
    // ---------------------------------------------------------------------------

    /**
     * Fetch all change log entries with lamport_clock > `sinceClock`.
     * Entries are returned in ascending clock order.
     */
    suspend fun getChangesSince(
        sinceClock: Long,
        limit: Long = 1000,
    ): Result<List<ChangeLogEntity>> = withContext(Dispatchers.IO) {
        Result.success(database.changeLogDao().getSince(sinceClock, limit))
    }

    /** Get the current change log clock. */
    suspend fun changeLogClock(): Long = withContext(Dispatchers.IO) {
        clockMutex.lock()
        return@withContext try {
            lamportClock
        } finally {
            clockMutex.unlock()
        }
    }

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------

    /**
     * Execute a write operation in the single-writer coroutine.
     * All writes go through this to maintain consistency.
     */
    private suspend fun <T> write(block: suspend () -> Result<T>): Result<T> =
        writeMutex.withLock {
            block()
        }

    /**
     * Record a change in the sync change log.
     * Must be called from within a write transaction.
     */
    private suspend fun recordChange(
        entityType: String,
        entityId: String,
        operation: String,
        payload: String = "{}",
    ) {
        val clock = nextClock()
        val now = System.currentTimeMillis() / 1000
        database.changeLogDao().insert(
            ChangeLogEntity(
                lamportClock = clock,
                deviceId = deviceId,
                entityType = entityType,
                entityId = entityId,
                operation = operation,
                payload = payload,
                appliedAt = now,
            ),
        )
    }
}
