// SPDX-License-Identifier: MPL-2.0
// Arrow Player — Room database migrations
// Spec: eclipse-player.md §9.4, REQ-LIB-001
//
// Migration numbers match the desktop database.cpp exactly.
// Each migration function does the same thing as its C++ counterpart.
//
// Migration 1  →  2  : add source_id, source_path columns
// Migration 2  →  3  : add discnumber, comment, bpm columns
// Migration 3  →  4  : add composer, grouping, music_key columns
// Migration 4  →  5  : add replaygain columns
// Migration 5  →  6  : add artwork columns
// Migration 6  →  7  : add lyrics columns
// Migration 7  →  8  : add library_folders table
// Migration 8  →  9  : add is_loved, is_blacklisted, play_count, skip_count, last_played_at
// Migration 9  → 10  : (no-op in this version)
// Migration 10 → 11  : add artist_sort_key, album_sort_key, album_hash
// Migration 11 → 12  : add cuesheets table
// Migration 12 → 13  : add cuetracks table
// Migration 13 → 14  : add playlists sort_order column
// Migration 14 → 15  : add change_log table
package io.github.arrowplayer.core.library.db

import androidx.room.migration.Migration
import androidx.sqlite.db.SupportSQLiteDatabase

// Migration 8: add library_folders table
@Suppress("ClassName")
val MIGRATION_7_8 = object : Migration(7, 8) {
    override fun migrate(db: SupportSQLiteDatabase) {
        db.execSQL(
            """
            CREATE TABLE IF NOT EXISTS library_folders (
                id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
                path TEXT NOT NULL UNIQUE COLLATE NOCASE,
                enabled INTEGER NOT NULL DEFAULT 1,
                scan_state TEXT NOT NULL DEFAULT 'idle',
                last_scan_at INTEGER,
                created_at INTEGER NOT NULL
            )
            """.trimIndent(),
        )
    }
}

// Migration 9: add user-state columns
@Suppress("ClassName")
val MIGRATION_8_9 = object : Migration(8, 9) {
    override fun migrate(db: SupportSQLiteDatabase) {
        db.execSQL("ALTER TABLE tracks ADD COLUMN is_loved INTEGER NOT NULL DEFAULT 0")
        db.execSQL("ALTER TABLE tracks ADD COLUMN is_blacklisted INTEGER NOT NULL DEFAULT 0")
        db.execSQL("ALTER TABLE tracks ADD COLUMN play_count INTEGER NOT NULL DEFAULT 0")
        db.execSQL("ALTER TABLE tracks ADD COLUMN skip_count INTEGER NOT NULL DEFAULT 0")
        db.execSQL("ALTER TABLE tracks ADD COLUMN last_played_at INTEGER")
    }
}

// Migration 11: add sort keys and album hash
@Suppress("ClassName")
val MIGRATION_10_11 = object : Migration(10, 11) {
    override fun migrate(db: SupportSQLiteDatabase) {
        db.execSQL("ALTER TABLE tracks ADD COLUMN artist_sort_key TEXT NOT NULL DEFAULT ''")
        db.execSQL("ALTER TABLE tracks ADD COLUMN album_sort_key TEXT NOT NULL DEFAULT ''")
        db.execSQL("ALTER TABLE tracks ADD COLUMN album_hash TEXT NOT NULL DEFAULT ''")
    }
}

// Migration 12: add cuesheets table
@Suppress("ClassName")
val MIGRATION_11_12 = object : Migration(11, 12) {
    override fun migrate(db: SupportSQLiteDatabase) {
        db.execSQL(
            """
            CREATE TABLE IF NOT EXISTS cuesheets (
                id TEXT NOT NULL PRIMARY KEY,
                file_path TEXT NOT NULL,
                file_device INTEGER NOT NULL DEFAULT 0,
                file_inode INTEGER NOT NULL DEFAULT 0,
                performer TEXT NOT NULL DEFAULT '',
                title TEXT NOT NULL DEFAULT '',
                file_ref TEXT NOT NULL,
                file_is_flac INTEGER NOT NULL DEFAULT 0,
                catalog TEXT NOT NULL DEFAULT '',
                created_at INTEGER NOT NULL
            )
            """.trimIndent(),
        )
        db.execSQL("CREATE INDEX IF NOT EXISTS idx_cuesheets_file_path ON cuesheets(file_path)")
    }
}

// Migration 13: add cuetracks table
@Suppress("ClassName")
val MIGRATION_12_13 = object : Migration(12, 13) {
    override fun migrate(db: SupportSQLiteDatabase) {
        db.execSQL(
            """
            CREATE TABLE IF NOT EXISTS cuetracks (
                id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
                cuesheet_id TEXT NOT NULL REFERENCES cuesheets(id) ON DELETE CASCADE,
                position INTEGER NOT NULL DEFAULT 0,
                title TEXT NOT NULL DEFAULT '',
                performer TEXT NOT NULL DEFAULT '',
                start_offset INTEGER NOT NULL DEFAULT 0,
                end_offset INTEGER NOT NULL DEFAULT 0,
                isrc TEXT NOT NULL DEFAULT '',
                flags TEXT NOT NULL DEFAULT ''
            )
            """.trimIndent(),
        )
        db.execSQL(
            "CREATE INDEX IF NOT EXISTS idx_cuetracks_cuesheet ON cuetracks(cuesheet_id, position)",
        )
    }
}

// Migration 14: add playlists sort_order column
@Suppress("ClassName")
val MIGRATION_13_14 = object : Migration(13, 14) {
    override fun migrate(db: SupportSQLiteDatabase) {
        db.execSQL("ALTER TABLE playlists ADD COLUMN sort_order INTEGER NOT NULL DEFAULT 0")
    }
}

// Migration 15: add change_log table
@Suppress("ClassName")
val MIGRATION_14_15 = object : Migration(14, 15) {
    override fun migrate(db: SupportSQLiteDatabase) {
        db.execSQL(
            """
            CREATE TABLE IF NOT EXISTS change_log (
                id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
                lamport_clock INTEGER NOT NULL,
                device_id TEXT NOT NULL,
                entity_type TEXT NOT NULL,
                entity_id TEXT NOT NULL,
                operation TEXT NOT NULL,
                payload TEXT NOT NULL,
                applied_at INTEGER NOT NULL
            )
            """.trimIndent(),
        )
        db.execSQL("CREATE INDEX IF NOT EXISTS idx_change_log_clock ON change_log(lamport_clock)")
        db.execSQL(
            "CREATE INDEX IF NOT EXISTS idx_change_log_entity ON change_log(entity_type, entity_id)",
        )
        db.execSQL(
            """
            CREATE INDEX IF NOT EXISTS idx_change_log_track
            ON change_log(entity_type, lamport_clock) WHERE entity_type = 'track'
            """.trimIndent(),
        )
        db.execSQL(
            """
            CREATE INDEX IF NOT EXISTS idx_change_log_playlist
            ON change_log(entity_type, lamport_clock) WHERE entity_type = 'playlist'
            """.trimIndent(),
        )
    }
}

/**
 * All migrations from version 1 to 15, in order.
 * Migrations 1–7 and 10 are no-ops on Android because those columns are added
 * in the initial schema (v1 includes the full spec schema from §9.4).
 */
val ALL_MIGRATIONS = listOf(
    MIGRATION_7_8,
    MIGRATION_8_9,
    MIGRATION_10_11,
    MIGRATION_11_12,
    MIGRATION_12_13,
    MIGRATION_13_14,
    MIGRATION_14_15,
)
