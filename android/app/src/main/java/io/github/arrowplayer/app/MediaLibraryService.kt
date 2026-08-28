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
//   androidx.car.app                      — AndroidX Car App library
//   com.google.android.googlequicksearchbox — Google Assistant

package io.github.arrowplayer.app

import androidx.annotation.OptIn
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.session.MediaLibraryService
import androidx.media3.session.MediaSession
import com.google.common.collect.ImmutableList
import com.google.common.util.concurrent.Futures
import com.google.common.util.concurrent.ListenableFuture

@OptIn(UnstableApi::class)
class MediaLibraryService : MediaLibraryService() {

    private lateinit var mediaLibrarySession: MediaLibrarySession

    override fun onCreate() {
        super.onCreate()
        val player = ExoPlayer.Builder(this).build()
        mediaLibrarySession = MediaLibrarySession.Builder(this, player)
            .build()
    }

    override fun onGetSession(controllerInfo: MediaSession.ControllerInfo): MediaLibrarySession {
        return mediaLibrarySession
    }

    override fun onGetLibraryRoot(
        controllerInfo: MediaSession.ControllerInfo,
        params: LibraryParams?,
    ): ListenableFuture<LibraryResult<MediaItem>> {
        if (!isPackageAllowed(controllerInfo.packageName)) {
            return Futures.immediateFuture(LibraryResult.ofError(LibraryResult.ERROR_PERMISSION_DENIED))
        }

        val root = MediaItem.Builder()
            .setMediaId(ROOT_ID)
            .setMediaMetadata(
                MediaMetadata.Builder()
                    .setTitle("Arrow Player")
                    .setIsPlayable(false)
                    .setIsBrowsable(true)
                    .build(),
            )
            .build()

        return Futures.immediateFuture(LibraryResult.ofItem(root))
    }

    override fun onGetChildren(
        controllerInfo: MediaSession.ControllerInfo,
        parentMediaId: String,
        page: Int,
        pageSize: Int,
        params: LibraryParams?,
    ): ListenableFuture<LibraryResult<ImmutableList<MediaItem>>> {
        if (!isPackageAllowed(controllerInfo.packageName)) {
            return Futures.immediateFuture(LibraryResult.ofError(LibraryResult.ERROR_PERMISSION_DENIED))
        }

        val children = when (parentMediaId) {
            ROOT_ID -> listOf(
                makeBrowsableTab(CHILDREN_ID_PLAYLISTS, "Playlists"),
                makeBrowsableTab(CHILDREN_ID_ALBUMS, "Albums"),
                makeBrowsableTab(CHILDREN_ID_ARTISTS, "Artists"),
                makeBrowsableTab(CHILDREN_ID_RECENT, "Recent"),
            )
            CHILDREN_ID_PLAYLISTS -> emptyList()
            CHILDREN_ID_ALBUMS -> emptyList()
            CHILDREN_ID_ARTISTS -> emptyList()
            CHILDREN_ID_RECENT -> emptyList()
            else -> return Futures.immediateFuture(LibraryResult.ofError(LibraryResult.ERROR_NOT_SUPPORTED))
        }

        return Futures.immediateFuture(LibraryResult.ofList(ImmutableList.copyOf(children)))
    }

    override fun onDestroy() {
        mediaLibrarySession.run {
            player.release()
            release()
        }
        super.onDestroy()
    }

    private fun isPackageAllowed(packageName: String?): Boolean {
        return packageName in ALLOWED_PACKAGES
    }

    private fun makeBrowsableTab(mediaId: String, title: String): MediaItem {
        return MediaItem.Builder()
            .setMediaId(mediaId)
            .setMediaMetadata(
                MediaMetadata.Builder()
                    .setTitle(title)
                    .setIsPlayable(false)
                    .setIsBrowsable(true)
                    .build(),
            )
            .build()
    }

    companion object {
        const val ROOT_ID = "/"
        const val CHILDREN_ID_PLAYLISTS = "/playlists"
        const val CHILDREN_ID_ALBUMS = "/albums"
        const val CHILDREN_ID_ARTISTS = "/artists"
        const val CHILDREN_ID_RECENT = "/recent"

        val ALLOWED_PACKAGES = setOf(
            "com.google.android.projection.gearhead",
            "com.google.android.car.kitchensink",
            "androidx.car.app",
            "com.google.android.googlequicksearchbox",
        )
    }
}
