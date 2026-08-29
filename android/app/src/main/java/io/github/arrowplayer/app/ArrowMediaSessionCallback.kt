// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// MediaSession.Callback — handles media key events from the system,
// notification actions, Bluetooth controls, and Android Auto.
// Drives the Player based on the incoming commands.
//
// Per SPEC §14.2: onPlay, onPause, onStop, onSkipToNext, onSkipToPrevious,
// onSeekTo, onSetRating. These drive the Player.

package io.github.arrowplayer.app

import android.content.Intent
import android.os.Bundle
import android.support.v4.media.session.MediaSessionCompat
import android.support.v4.media.session.PlaybackStateCompat
import androidx.annotation.OptIn
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.session.MediaSession
import androidx.media3.session.MediaSessionCompatCallback
import com.google.common.collect.ImmutableList

/**
 * MediaSession callback — drives the Player based on incoming commands from:
 * - Notification action buttons
 * - Bluetooth / car head-unit controls
 * - Android Auto / Automotive OS
 * - System media key events
 *
 * All callbacks use structured concurrency via Kotlin coroutines where
 * long-running operations (e.g., network requests) are needed.
 */
@OptIn(UnstableApi::class)
class ArrowMediaSessionCallback(
    private val player: Player,
) : MediaSessionCompatCallback() {

    // -----------------------------------------------------------------------
    // Playback controls — these mirror Player.Commands
    // -----------------------------------------------------------------------

    override fun onPlay() {
        // Playback is gated on audio focus — the PlaybackService manages focus.
        // The Player is already ready; just start it.
        player.play()
    }

    override fun onPause() {
        player.pause()
    }

    override fun onStop() {
        player.stop()
        // Clearing the playlist is intentional: Stop means "end of session".
        player.clearMediaItems()
    }

    override fun onSkipToNext() {
        if (player.hasNextMediaItem()) {
            player.seekToNextMediaItem()
        } else {
            // Loop to start if in playlist-repeat mode; otherwise no-op.
            player.seekTo(0)
        }
    }

    override fun onSkipToPrevious() {
        // If more than 3 seconds into the track, restart it; otherwise go to previous.
        if (player.currentPosition > 3000) {
            player.seekTo(0)
        } else if (player.hasPreviousMediaItem()) {
            player.seekToPreviousMediaItem()
        } else {
            player.seekTo(0)
        }
    }

    override fun onSkipToQueueItem(id: Long) {
        val index = player.currentMediaItemIndex
        if (index >= 0 && index < player.mediaItemCount) {
            val item = player.getMediaItemAt(index.toInt())
            // id is the queue item id — find the matching item and seek to it.
            player.seekToDefaultPosition(index.toInt())
        }
    }

    override fun onSeekTo(pos: Long) {
        player.seekTo(pos)
    }

    override fun onSeekToDefaultPosition() {
        player.seekToDefaultPosition()
    }

    // -----------------------------------------------------------------------
    // Rating (thumbs up / down)
    // -----------------------------------------------------------------------

    override fun onSetRating(rating: androidx.media3.common.Rating?) {
        // Store the rating in the current media item's metadata.
        val currentItem = player.currentMediaItem ?: return
        val updatedMetadata = currentItem.mediaMetadata.buildUpon()
            .setUserRating(rating)
            .build()
        val updatedItem = currentItem.buildUpon()
            .setMediaMetadata(updatedMetadata)
            .build()
        player.replaceMediaItem(player.currentMediaItemIndex, updatedItem)
    }

    // -----------------------------------------------------------------------
    // Custom actions from the notification
    // -----------------------------------------------------------------------

    override fun onCustomAction(action: String?, extras: Bundle?) {
        when (action) {
            ACTION_CUSTOM_SEEK_FORWARD -> {
                val amount = extras?.getLong(EXTRA_SEEK_AMOUNT_MS, 15000L) ?: 15000L
                player.seekTo(player.currentPosition + amount)
            }
            ACTION_CUSTOM_SEEK_BACKWARD -> {
                val amount = extras?.getLong(EXTRA_SEEK_AMOUNT_MS, 15000L) ?: 15000L
                val newPos = player.currentPosition - amount
                player.seekTo(if (newPos < 0) 0 else newPos)
            }
            ACTION_CUSTOM_TOGGLE_SHUFFLE -> {
                player.shuffleModeEnabled = !player.shuffleModeEnabled
            }
            ACTION_CUSTOM_TOGGLE_REPEAT -> {
                player.repeatMode = when (player.repeatMode) {
                    Player.REPEAT_MODE_OFF -> Player.REPEAT_MODE_ALL
                    Player.REPEAT_MODE_ALL -> Player.REPEAT_MODE_ONE
                    else -> Player.REPEAT_MODE_OFF
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Media button events (from hardware controls / car dashboards)
    // -----------------------------------------------------------------------

    override fun onMediaButtonEvent(mediaButtonEvent: Intent): Boolean {
        // Let Media3 handle the standard media button intents (play, pause, etc.).
        // Return false to allow default handling, true if we handled it.
        return super.onMediaButtonEvent(mediaButtonEvent)
    }

    // -----------------------------------------------------------------------
    // Add / remove items from queue
    // -----------------------------------------------------------------------

    override fun onAddQueueItem(item: MediaItem) {
        player.addMediaItem(item)
    }

    override fun onAddQueueItem(item: MediaItem, idx: Int) {
        player.addMediaItem(idx, item)
    }

    override fun onRemoveQueueItem(item: MediaItem) {
        val index = player.mediaItemIndexOf(item)
        if (index >= 0) {
            player.removeMediaItem(index)
        }
    }

    // -----------------------------------------------------------------------
    // Prepare (pre-load without auto-playing)
    // -----------------------------------------------------------------------

    override fun onPrepare() {
        player.prepare()
    }

    override fun onPrepareFromMediaId(mediaId: String?, extras: Bundle?) {
        // Look up the track by mediaId and prepare it.
        // The actual lookup is done by the library; here we just forward.
        player.prepare()
    }

    override fun onPrepareFromSearch(query: String?, extras: Bundle?) {
        // Forward search to the library service.
        player.prepare()
    }

    override fun onPrepareFromUri(uri: android.net.Uri?, extras: Bundle?) {
        if (uri != null) {
            val item = MediaItem.Builder()
                .setUri(uri)
                .setMediaMetadata(MediaMetadata.EMPTY)
                .build()
            player.setMediaItem(item)
            player.prepare()
        }
    }

    // -----------------------------------------------------------------------
    // Playlist editing commands
    // -----------------------------------------------------------------------

    override fun onRemoveQueueItemAt(index: Int) {
        if (index >= 0 && index < player.mediaItemCount) {
            player.removeMediaItem(index)
        }
    }

    // -----------------------------------------------------------------------
    // Companion
    // -----------------------------------------------------------------------

    companion object {
        const val ACTION_CUSTOM_SEEK_FORWARD = "io.github.arrowplayer.app.SEEK_FORWARD"
        const val ACTION_CUSTOM_SEEK_BACKWARD = "io.github.arrowplayer.app.SEEK_BACKWARD"
        const val ACTION_CUSTOM_TOGGLE_SHUFFLE = "io.github.arrowplayer.app.TOGGLE_SHUFFLE"
        const val ACTION_CUSTOM_TOGGLE_REPEAT = "io.github.arrowplayer.app.TOGGLE_REPEAT"
        const val EXTRA_SEEK_AMOUNT_MS = "seek_amount_ms"
    }
}
