// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// MediaLibraryService — Phase 1 (REQ-AUT-001 .. REQ-AUT-021).
//
// Exposes the music catalogue to Android Auto via Media3's
// MediaLibrarySession. The browse tree root has up to 4 tabs
// per REQ-AUT-004: Playlists, Albums, Artists, Recent.
//
// Package validation per REQ-AUT-002:
//   com.google.android.projection.gearhead  — Android Auto (car head unit)
//   com.google.android.car.kitchensink      — Auto test harness
//   androidx.car.app                        — AndroidX Car App library
//   com.google.android.googlequicksearchbox — Google Assistant

package io.github.arrowplayer.app

import android.content.Intent
import androidx.annotation.OptIn
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.session.MediaLibraryService
import androidx.media3.session.MediaSession
import androidx.media3.session.LibraryResult

@OptIn(UnstableApi::class)
class MediaLibraryService : MediaLibraryService() {

    private var mediaLibrarySession: MediaLibrarySession? = null

    override fun onCreate() {
        super.onCreate()

        val player = ExoPlayer.Builder(this).build()

        mediaLibrarySession = MediaLibrarySession.Builder(this, player, LibraryCallback())
            .build()
    }

    override fun onGetSession(controllerInfo: MediaSession.ControllerInfo): MediaLibrarySession? {
        return mediaLibrarySession
    }

    override fun onDestroy() {
        mediaLibrarySession?.run {
            player.release()
            release()
        }
        mediaLibrarySession = null
        super.onDestroy()
    }

    override fun onGetLibraryRoot(
        controllerInfo: MediaSession.ControllerInfo,
        immediate: Boolean,
        params: LibraryParams?,
    ): LibraryResult<MediaItem> {
        if (!isPackageAllowed(controllerInfo.packageName)) {
            return LibraryResult.error(LibraryResult.ERROR_PERMISSION_DENIED)
        }

        val root = MediaItem.Builder()
            .setMediaId(ROOT_ID)
            .setMediaMetadata(
                androidx.media3.common.MediaMetadata.Builder()
                    .setTitle("Arrow Player")
                    .setIsPlayable(false)
                    .setIsBrowsable(true)
                    .build(),
            )
            .build()

        return LibraryResult.item(root)
    }

    override fun onGetChildren(
        controllerInfo: MediaSession.ControllerInfo,
        parentMediaId: String,
        page: Int,
        pageSize: Int,
        params: LibraryParams?,
    ): LibraryResult<kotlin.collections.List<MediaItem>> {
        if (!isPackageAllowed(controllerInfo.packageName)) {
            return LibraryResult.error(LibraryResult.ERROR_PERMISSION_DENIED)
        }

        val children = when (parentMediaId) {
            ROOT_ID -> listOf(
                makeBrowsableTab(CHILDREN_ID_PLAYLISTS, "Playlists"),
                makeBrowsableTab(CHILDREN_ID_ALBUMS, "Albums"),
                makeBrowsableTab(CHILDREN_ID_ARTISTS, "Artists"),
                makeBrowsableTab(CHILDREN_ID_RECENT, "Recent"),
            )
            CHILDREN_ID_PLAYLISTS -> emptyList() // Phase 1 stub — populated from library
            CHILDREN_ID_ALBUMS -> emptyList()    // Phase 1 stub — populated from library
            CHILDREN_ID_ARTISTS -> emptyList()   // Phase 1 stub — populated from library
            CHILDREN_ID_RECENT -> emptyList()    // Phase 1 stub — populated from library
            else -> return LibraryResult.error(LibraryResult.ERROR_NOT_SUPPORTED)
        }

        return LibraryResult.items(children)
    }

    private fun isPackageAllowed(packageName: String?): Boolean {
        return packageName in ALLOWED_PACKAGES
    }

    private fun makeBrowsableTab(mediaId: String, title: String): MediaItem {
        return MediaItem.Builder()
            .setMediaId(mediaId)
            .setMediaMetadata(
                androidx.media3.common.MediaMetadata.Builder()
                    .setTitle(title)
                    .setIsPlayable(false)
                    .setIsBrowsable(true)
                    .build(),
            )
            .build()
    }

    private inner class LibraryCallback : MediaLibrarySession.Callback {
        override fun onGetLibraryRoot(
            session: MediaLibrarySession,
            browser: MediaSession.ControllerInfo,
            immediate: Boolean,
            params: LibraryParams?,
        ): LibraryResult<MediaItem> {
            return this@MediaLibraryService.onGetLibraryRoot(browser, immediate, params)
        }

        override fun onGetChildren(
            session: MediaLibrarySession,
            browser: MediaSession.ControllerInfo,
            parentMediaId: String,
            page: Int,
            pageSize: Int,
            params: LibraryParams?,
        ): LibraryResult<kotlin.collections.List<MediaItem>> {
            return this@MediaLibraryService.onGetChildren(browser, parentMediaId, page, pageSize, params)
        }
    }

    companion object {
        const val ROOT_ID = "/"
        const val CHILDREN_ID_PLAYLISTS = "/playlists"
        const val CHILDREN_ID_ALBUMS = "/albums"
        const val CHILDREN_ID_ARTISTS = "/artists"
        const val CHILDREN_ID_RECENT = "/recent"

        // REQ-AUT-002: validated calling package names
        val ALLOWED_PACKAGES = setOf(
            "com.google.android.projection.gearhead",   // Android Auto (car)
            "com.google.android.car.kitchensink",        // Auto test harness
            "androidx.car.app",                          // AndroidX Car App
            "com.google.android.googlequicksearchbox",   // Google Assistant
        )
    }
}
