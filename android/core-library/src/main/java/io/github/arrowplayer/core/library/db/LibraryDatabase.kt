// SPDX-License-Identifier: MPL-2.0
// Arrow Player — Room library database
// Spec: eclipse-player.md §9.4 (REQ-LIB-001), ADR 0007
//
// Android Room implementation of the shared library database schema.
// This file is the canonical Kotlin entity definitions; the DDL in
// desktop/src/library/schema.sql is the source of truth.
//
// Migration numbers match the desktop schema exactly (REQ-LIB-001).
package io.github.arrowplayer.core.library.db

import androidx.room.ColumnInfo
import androidx.room.Dao
import androidx.room.Database
import androidx.room.Delete
import androidx.room.Entity
import androidx.room.Fts4
import androidx.room.ForeignKey
import androidx.room.Index
import androidx.room.Insert
import androidx.room.OnConflictStrategy
import androidx.room.PrimaryKey
import androidx.room.Query
import androidx.room.RoomDatabase
import androidx.room.Transaction
import androidx.room.Update
import kotlinx.coroutines.flow.Flow

// ===========================================================================
// Schema version 15 — matches desktop/src/library/schema.sql exactly
// ===========================================================================

/** Current schema version. Must match CURRENT_SCHEMA_VERSION in database.cpp */
const val SCHEMA_VERSION = 15

// ===========================================================================
// Tracks
// ===========================================================================

/**
 * Full track record. Mirrors the `tracks` table in schema.sql exactly.
 * Every column, type, and constraint matches the DDL.
 *
 * @property id Auto-incrementing primary key
 * @property containerPath Absolute, canonical file path (unique, case-insensitive)
 * @property filename Filename without the directory part
 * @property fileSize File size in bytes
 * @property durationMs Duration in milliseconds (REQ-LIB-025)
 * @property title Track title
 * @property artist Artist name
 * @property album Album name
 * @property albumartist Album artist
 * @property genre Genre
 * @property year Release year
 * @property trackNumber Track number string (may contain "1/12")
 * @property discNumber Disc number string
 * @property comment User comment
 * @property bpm Beats per minute (REQ-LIB-025)
 * @property composer Composer
 * @property grouping Grouping / work
 * @property musicKey Musical key (REQ-LIB-025)
 * @property rgTrackGain ReplayGain track gain in dB (REQ-LIB-027)
 * @property rgTrackPeak ReplayGain track peak
 * @property rgAlbumGain ReplayGain album gain in dB
 * @property rgAlbumPeak ReplayGain album peak
 * @property bitrateKbps Bitrate in kbps
 * @property sampleRate Sample rate in Hz
 * @property bitDepth Bit depth in bits
 * @property channels Number of audio channels
 * @property codec Audio codec name (FLAC, MP3, Opus, etc.)
 * @property container Container format (MPEG, Ogg, etc.)
 * @property isLossless True for lossless formats
 * @property artworkId Content hash of embedded artwork, or null (REQ-LIB-032)
 * @property hasArtwork True if artwork is present
 * @property lyricsId Content hash of lyrics, or null
 * @property hasLyrics True if lyrics are present
 * @property sourceId Source/URL identifier
 * @property sourcePath Source/URL path
 * @property rating Rating 0–100 (REQ-LIB-070)
 * @property isLoved True if loved (REQ-LIB-070)
 * @property isBlacklisted True if blacklisted
 * @property playCount Playback count (REQ-LIB-070)
 * @property skipCount Skip count
 * @property lastPlayedAt Unix epoch seconds of last playback, or null
 * @property artistSortKey NFKD-normalised, diacritic-stripped artist sort key (REQ-LIB-029)
 * @property albumSortKey NFKD-normalised, diacritic-stripped album sort key
 * @property albumHash Album identity hash (REQ-LIB-031)
 * @property cuesheetId Cuesheet record ID, or null (REQ-LIB-040)
 * @property addedAt Unix epoch seconds when added
 * @property updatedAt Unix epoch seconds of last tag/content update
 * @property deletedAt Unix epoch seconds of soft deletion, or null
 * @property fileDevice Device number for symlink-loop detection (REQ-LIB-052)
 * @property fileInode Inode number for symlink-loop detection
 */
@Entity(
    tableName = "tracks",
    indices = [
        Index("container_path"),
        Index("artist"),
        Index("album"),
        Index("albumartist"),
        Index("genre"),
        Index("year"),
        Index("added_at"),
        Index("rating"),
        Index("play_count"),
        Index("last_played_at"),
        Index("source_id", whereClause = "source_id != ''"),
        Index("album_hash", whereClause = "album_hash != ''"),
        Index("deleted_at", whereClause = "deleted_at IS NOT NULL"),
        Index(value = ["file_device", "file_inode"], whereClause = "file_device != 0"),
    ],
)
@Suppress("PropertyName", "LongParameterList")
data class TrackEntity(
    @PrimaryKey(autoGenerate = true)
    @ColumnInfo(name = "id")
    val id: Long = 0,

    @ColumnInfo(name = "container_path")
    val containerPath: String,

    @ColumnInfo(name = "filename")
    val filename: String,

    @ColumnInfo(name = "file_size")
    val fileSize: Long = 0,

    @ColumnInfo(name = "duration_ms")
    val durationMs: Long = 0,

    @ColumnInfo(name = "title")
    val title: String = "",

    @ColumnInfo(name = "artist")
    val artist: String = "",

    @ColumnInfo(name = "album")
    val album: String = "",

    @ColumnInfo(name = "albumartist")
    val albumartist: String = "",

    @ColumnInfo(name = "genre")
    val genre: String = "",

    @ColumnInfo(name = "year")
    val year: Int = 0,

    @ColumnInfo(name = "tracknumber")
    val tracknumber: String = "",

    @ColumnInfo(name = "discnumber")
    val discnumber: String = "",

    @ColumnInfo(name = "comment")
    val comment: String = "",

    @ColumnInfo(name = "bpm")
    val bpm: Double = 0.0,

    @ColumnInfo(name = "composer")
    val composer: String = "",

    @ColumnInfo(name = "grouping")
    val grouping: String = "",

    @ColumnInfo(name = "music_key")
    val musicKey: String = "",

    @ColumnInfo(name = "rg_track_gain")
    val rgTrackGain: Double = 0.0,

    @ColumnInfo(name = "rg_track_peak")
    val rgTrackPeak: Double = 0.0,

    @ColumnInfo(name = "rg_album_gain")
    val rgAlbumGain: Double = 0.0,

    @ColumnInfo(name = "rg_album_peak")
    val rgAlbumPeak: Double = 0.0,

    @ColumnInfo(name = "bitrate_kbps")
    val bitrateKbps: Int = 0,

    @ColumnInfo(name = "sample_rate")
    val sampleRate: Int = 0,

    @ColumnInfo(name = "bit_depth")
    val bitDepth: Int = 0,

    @ColumnInfo(name = "channels")
    val channels: Int = 0,

    @ColumnInfo(name = "codec")
    val codec: String = "",

    @ColumnInfo(name = "container")
    val container: String = "",

    @ColumnInfo(name = "is_lossless")
    val isLossless: Boolean = false,

    @ColumnInfo(name = "artwork_id")
    val artworkId: String? = null,

    @ColumnInfo(name = "has_artwork")
    val hasArtwork: Boolean = false,

    @ColumnInfo(name = "lyrics_id")
    val lyricsId: String? = null,

    @ColumnInfo(name = "has_lyrics")
    val hasLyrics: Boolean = false,

    @ColumnInfo(name = "source_id")
    val sourceId: String = "",

    @ColumnInfo(name = "source_path")
    val sourcePath: String = "",

    @ColumnInfo(name = "rating")
    val rating: Int = 0,

    @ColumnInfo(name = "is_loved")
    val isLoved: Boolean = false,

    @ColumnInfo(name = "is_blacklisted")
    val isBlacklisted: Boolean = false,

    @ColumnInfo(name = "play_count")
    val playCount: Long = 0,

    @ColumnInfo(name = "skip_count")
    val skipCount: Long = 0,

    @ColumnInfo(name = "last_played_at")
    val lastPlayedAt: Long? = null,

    @ColumnInfo(name = "artist_sort_key")
    val artistSortKey: String = "",

    @ColumnInfo(name = "album_sort_key")
    val albumSortKey: String = "",

    @ColumnInfo(name = "album_hash")
    val albumHash: String = "",

    @ColumnInfo(name = "cuesheet_id")
    val cuesheetId: String? = null,

    @ColumnInfo(name = "added_at")
    val addedAt: Long,

    @ColumnInfo(name = "updated_at")
    val updatedAt: Long,

    @ColumnInfo(name = "deleted_at")
    val deletedAt: Long? = null,

    @ColumnInfo(name = "file_device")
    val fileDevice: Long = 0,

    @ColumnInfo(name = "file_inode")
    val fileInode: Long = 0,
)

// ===========================================================================
// Tracks FTS
// ===========================================================================

/**
 * FTS5 virtual table for full-text search on tracks.
 * Content table is `tracks`. Tokenizer: unicode61 with diacritics removed.
 * Matches the `tracks_fts` virtual table in schema.sql.
 */
@Fts4(contentEntity = TrackEntity::class)
@Entity(tableName = "tracks_fts")
data class TrackFtsEntity(
    val title: String,
    val artist: String,
    val album: String,
    val albumartist: String,
    val genre: String,
    val composer: String,
)

// ===========================================================================
// Library folders
// ===========================================================================

/**
 * A library scan root directory.
 * Matches `library_folders` table in schema.sql.
 */
@Entity(
    tableName = "library_folders",
    indices = [Index(value = ["path"], unique = true)],
)
data class LibraryFolderEntity(
    @PrimaryKey(autoGenerate = true)
    @ColumnInfo(name = "id")
    val id: Long = 0,

    @ColumnInfo(name = "path")
    val path: String,

    @ColumnInfo(name = "enabled")
    val enabled: Boolean = true,

    @ColumnInfo(name = "scan_state")
    val scanState: String = "idle",

    @ColumnInfo(name = "last_scan_at")
    val lastScanAt: Long? = null,

    @ColumnInfo(name = "created_at")
    val createdAt: Long,
)

// ===========================================================================
// Cue sheets (REQ-LIB-040)
// ===========================================================================

/**
 * A parsed cue sheet. The ID is a hash of the file content.
 * Matches `cuesheets` table in schema.sql.
 */
@Entity(
    tableName = "cuesheets",
    primaryKeys = ["id"],
    indices = [Index("file_path")],
)
data class CuesheetEntity(
    @ColumnInfo(name = "id")
    val id: String,

    @ColumnInfo(name = "file_path")
    val filePath: String,

    @ColumnInfo(name = "file_device")
    val fileDevice: Long = 0,

    @ColumnInfo(name = "file_inode")
    val fileInode: Long = 0,

    @ColumnInfo(name = "performer")
    val performer: String = "",

    @ColumnInfo(name = "title")
    val title: String = "",

    @ColumnInfo(name = "file_ref")
    val fileRef: String,

    @ColumnInfo(name = "file_is_flac")
    val fileIsFlac: Boolean = false,

    @ColumnInfo(name = "catalog")
    val catalog: String = "",

    @ColumnInfo(name = "created_at")
    val createdAt: Long,
)

/**
 * One entry within a cue sheet (a track within the cue).
 * Matches `cuetracks` table in schema.sql (REQ-LIB-042).
 */
@Entity(
    tableName = "cuetracks",
    primaryKeys = ["id"],
    foreignKeys = [
        ForeignKey(
            entity = CuesheetEntity::class,
            parentColumns = ["id"],
            childColumns = ["cuesheet_id"],
            onDelete = ForeignKey.CASCADE,
        ),
    ],
    indices = [Index("cuesheet_id")],
)
data class CuetrackEntity(
    @PrimaryKey(autoGenerate = true)
    @ColumnInfo(name = "id")
    val id: Long = 0,

    @ColumnInfo(name = "cuesheet_id")
    val cuesheetId: String,

    @ColumnInfo(name = "position")
    val position: Int = 0,

    @ColumnInfo(name = "title")
    val title: String = "",

    @ColumnInfo(name = "performer")
    val performer: String = "",

    @ColumnInfo(name = "start_offset")
    val startOffset: Long = 0,

    @ColumnInfo(name = "end_offset")
    val endOffset: Long = 0,

    @ColumnInfo(name = "isrc")
    val isrc: String = "",

    @ColumnInfo(name = "flags")
    val flags: String = "",
)

// ===========================================================================
// Custom / user-defined tags (REQ-LIB-033)
// ===========================================================================

/**
 * A custom tag value attached to a track.
 * Multiple values per (track, key) are allowed.
 * Matches `track_custom_tags` table in schema.sql.
 */
@Entity(
    tableName = "track_custom_tags",
    primaryKeys = ["track_id", "key", "value"],
    foreignKeys = [
        ForeignKey(
            entity = TrackEntity::class,
            parentColumns = ["id"],
            childColumns = ["track_id"],
            onDelete = ForeignKey.CASCADE,
        ),
    ],
    indices = [Index("key")],
)
data class CustomTagEntity(
    @ColumnInfo(name = "track_id")
    val trackId: Long,

    @ColumnInfo(name = "key")
    val key: String,

    @ColumnInfo(name = "value")
    val value: String,

    @ColumnInfo(name = "updated_at")
    val updatedAt: Long,
)

// ===========================================================================
// Playlists
// ===========================================================================

/**
 * A playlist. Can be manual (kind=0) or smart (kind=1).
 * Matches `playlists` table in schema.sql.
 */
@Entity(
    tableName = "playlists",
    indices = [
        Index(value = ["uuid"], unique = true),
        Index("kind"),
    ],
)
data class PlaylistEntity(
    @PrimaryKey(autoGenerate = true)
    @ColumnInfo(name = "id")
    val id: Long = 0,

    @ColumnInfo(name = "uuid")
    val uuid: String,

    @ColumnInfo(name = "name")
    val name: String,

    @ColumnInfo(name = "description")
    val description: String = "",

    @ColumnInfo(name = "kind")
    val kind: Int,  // 0=manual, 1=smart

    @ColumnInfo(name = "rule_json")
    val ruleJson: String? = null,

    @ColumnInfo(name = "auto_refresh")
    val autoRefresh: Boolean = true,

    @ColumnInfo(name = "sort_order")
    val sortOrder: Long = 0,

    @ColumnInfo(name = "created_at")
    val createdAt: Long,

    @ColumnInfo(name = "updated_at")
    val updatedAt: Long,
)

/**
 * One item within a playlist.
 * Matches `playlist_items` table in schema.sql.
 */
@Entity(
    tableName = "playlist_items",
    primaryKeys = ["playlist_id", "position"],
    foreignKeys = [
        ForeignKey(
            entity = PlaylistEntity::class,
            parentColumns = ["id"],
            childColumns = ["playlist_id"],
            onDelete = ForeignKey.CASCADE,
        ),
        ForeignKey(
            entity = TrackEntity::class,
            parentColumns = ["id"],
            childColumns = ["track_id"],
            onDelete = ForeignKey.CASCADE,
        ),
    ],
    indices = [Index("track_id")],
)
data class PlaylistItemEntity(
    @ColumnInfo(name = "playlist_id")
    val playlistId: Long,

    @ColumnInfo(name = "position")
    val position: Int,

    @ColumnInfo(name = "track_id")
    val trackId: Long,

    @ColumnInfo(name = "added_at")
    val addedAt: Long,
)

// ===========================================================================
// Sync change log (REQ-LIB-075)
// ===========================================================================

/**
 * Full-history append-only sync change log.
 * Lamport clock provides total ordering across devices.
 * Matches `change_log` table in schema.sql.
 */
@Entity(
    tableName = "change_log",
    indices = [
        Index("lamport_clock"),
        Index(value = ["entity_type", "entity_id"]),
        Index(value = ["entity_type", "lamport_clock"], whereClause = "entity_type = 'track'"),
        Index(value = ["entity_type", "lamport_clock"], whereClause = "entity_type = 'playlist'"),
    ],
)
data class ChangeLogEntity(
    @PrimaryKey(autoGenerate = true)
    @ColumnInfo(name = "id")
    val id: Long = 0,

    @ColumnInfo(name = "lamport_clock")
    val lamportClock: Long,

    @ColumnInfo(name = "device_id")
    val deviceId: String,

    @ColumnInfo(name = "entity_type")
    val entityType: String,  // 'track' | 'playlist' | 'playlist_item' | 'custom_tag'

    @ColumnInfo(name = "entity_id")
    val entityId: String,

    @ColumnInfo(name = "operation")
    val operation: String,  // 'insert' | 'update' | 'delete'

    @ColumnInfo(name = "payload")
    val payload: String,  // JSON, or empty on delete

    @ColumnInfo(name = "applied_at")
    val appliedAt: Long,
)

// ===========================================================================
// Schema version tracking
// ===========================================================================

@Entity(tableName = "schema_version")
data class SchemaVersionEntity(
    @PrimaryKey
    @ColumnInfo(name = "version")
    val version: Int,
)

// ===========================================================================
// DAOs
// ===========================================================================

/** Data access object for tracks. */
@Dao
interface TrackDao {
    @Query("SELECT * FROM tracks WHERE deleted_at IS NULL ORDER BY id LIMIT :limit OFFSET :offset")
    fun list(offset: Long, limit: Long): Flow<List<TrackEntity>>

    @Query("SELECT * FROM tracks WHERE deleted_at IS NULL ORDER BY id LIMIT :limit OFFSET :offset")
    suspend fun listSync(offset: Long, limit: Long): List<TrackEntity>

    @Query("SELECT container_path FROM tracks WHERE deleted_at IS NULL")
    suspend fun allPaths(): List<String>

    @Query("SELECT * FROM tracks WHERE container_path = :path AND deleted_at IS NULL LIMIT 1")
    suspend fun byPath(path: String): TrackEntity?

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun upsert(track: TrackEntity)

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun upsertAll(tracks: List<TrackEntity>)

    @Query("UPDATE tracks SET deleted_at = :deletedAt WHERE container_path = :path AND deleted_at IS NULL")
    suspend fun markGone(path: String, deletedAt: Long)

    @Query("UPDATE tracks SET deleted_at = :deletedAt WHERE container_path = :path AND deleted_at IS NULL")
    suspend fun markGoneBatch(paths: List<String>, deletedAt: Long)

    @Query("SELECT COUNT(*) FROM tracks WHERE deleted_at IS NULL")
    fun countFlow(): Flow<Long>

    @Query("SELECT COUNT(*) FROM tracks WHERE deleted_at IS NULL")
    suspend fun countSync(): Long
}

/** Data access object for library folders. */
@Dao
interface LibraryFolderDao {
    @Query("SELECT * FROM library_folders ORDER BY id")
    fun listAll(): Flow<List<LibraryFolderEntity>>

    @Query("SELECT * FROM library_folders ORDER BY id")
    suspend fun listAllSync(): List<LibraryFolderEntity>

    @Insert(onConflict = OnConflictStrategy.FAIL)
    suspend fun insert(folder: LibraryFolderEntity): Long

    @Update
    suspend fun update(folder: LibraryFolderEntity)

    @Query("DELETE FROM library_folders WHERE id = :id")
    suspend fun delete(id: Long)
}

/** Data access object for cue sheets. */
@Dao
interface CuesheetDao {
    @Query("SELECT * FROM cuesheets WHERE id = :id")
    suspend fun byId(id: String): CuesheetEntity?

    @Query("SELECT * FROM cuesheets WHERE file_path = :path")
    suspend fun byPath(path: String): CuesheetEntity?

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun upsert(cuesheet: CuesheetEntity)

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertTracks(tracks: List<CuetrackEntity>)

    @Query("DELETE FROM cuetracks WHERE cuesheet_id = :cuesheetId")
    suspend fun deleteTracks(cuesheetId: String)

    @Query("SELECT * FROM cuetracks WHERE cuesheet_id = :cuesheetId ORDER BY position")
    suspend fun getTracks(cuesheetId: String): List<CuetrackEntity>
}

/** Data access object for custom tags. */
@Dao
interface CustomTagDao {
    @Query("SELECT * FROM track_custom_tags WHERE track_id = :trackId ORDER BY key, value")
    fun listForTrack(trackId: Long): Flow<List<CustomTagEntity>>

    @Query("SELECT * FROM track_custom_tags WHERE track_id = :trackId ORDER BY key, value")
    suspend fun listForTrackSync(trackId: Long): List<CustomTagEntity>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insert(tag: CustomTagEntity)

    @Query("DELETE FROM track_custom_tags WHERE track_id = :trackId AND key = :key AND value = :value")
    suspend fun delete(trackId: Long, key: String, value: String)
}

/** Data access object for playlists. */
@Dao
interface PlaylistDao {
    @Query("SELECT * FROM playlists ORDER BY sort_order ASC, id ASC")
    fun listAll(): Flow<List<PlaylistEntity>>

    @Query("SELECT * FROM playlists ORDER BY sort_order ASC, id ASC")
    suspend fun listAllSync(): List<PlaylistEntity>

    @Query("SELECT * FROM playlists WHERE id = :id")
    suspend fun byId(id: Long): PlaylistEntity?

    @Query("SELECT * FROM playlists WHERE uuid = :uuid")
    suspend fun byUuid(uuid: String): PlaylistEntity?

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun upsert(playlist: PlaylistEntity): Long

    @Query("DELETE FROM playlists WHERE id = :id")
    suspend fun delete(id: Long)

    @Query("SELECT * FROM playlist_items WHERE playlist_id = :playlistId ORDER BY position")
    fun items(playlistId: Long): Flow<List<PlaylistItemEntity>>

    @Query("SELECT * FROM playlist_items WHERE playlist_id = :playlistId ORDER BY position")
    suspend fun itemsSync(playlistId: Long): List<PlaylistItemEntity>

    @Query("DELETE FROM playlist_items WHERE playlist_id = :playlistId")
    suspend fun deleteItems(playlistId: Long)

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertItems(items: List<PlaylistItemEntity>)
}

/** Data access object for the sync change log. */
@Dao
interface ChangeLogDao {
    @Query("SELECT * FROM change_log WHERE lamport_clock > :sinceClock ORDER BY lamport_clock ASC LIMIT :limit")
    suspend fun getSince(sinceClock: Long, limit: Long): List<ChangeLogEntity>

    @Query("SELECT COALESCE(MAX(lamport_clock), 0) FROM change_log")
    suspend fun maxClock(): Long

    @Insert
    suspend fun insert(entry: ChangeLogEntity)
}

// ===========================================================================
// Database
// ===========================================================================

/**
 * Arrow Player library database.
 * Migration 1–15 match the desktop schema migrations exactly (REQ-LIB-001).
 * Room schema is exported to core-library/schemas/ for CI comparison (ADR 0007).
 */
@Database(
    version = SCHEMA_VERSION,
    entities = [
        TrackEntity::class,
        TrackFtsEntity::class,
        LibraryFolderEntity::class,
        CuesheetEntity::class,
        CuetrackEntity::class,
        CustomTagEntity::class,
        PlaylistEntity::class,
        PlaylistItemEntity::class,
        ChangeLogEntity::class,
        SchemaVersionEntity::class,
    ],
    exportSchema = true,
)
abstract class LibraryDatabase : RoomDatabase() {
    abstract fun trackDao(): TrackDao
    abstract fun libraryFolderDao(): LibraryFolderDao
    abstract fun cuesheetDao(): CuesheetDao
    abstract fun customTagDao(): CustomTagDao
    abstract fun playlistDao(): PlaylistDao
    abstract fun changeLogDao(): ChangeLogDao
}
