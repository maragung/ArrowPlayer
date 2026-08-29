// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// PlaybackService — Phase 2 (REQ-OSI-040, REQ-OSI-041, REQ-OSI-042).
//
// Foreground service of type mediaPlayback that owns the MediaSession,
// audio focus, and the becoming-noisy broadcast receiver. It bridges
// the in-process audio engine to the system session surface (lock
// screen, notification, Android Auto, Bluetooth controls).
//
// Lifecycle (REQ-OSI-040 note, §14.1):
//   - onCreate  : create MediaSession, register AudioFocusController,
//                 register BecomingNoisyReceiver
//   - onStartCommand: startForeground with the notification,
//                    return STICKY if there is an active queue
//   - onTaskRemoved: stopSelf if the queue is empty (not every removal)
//   - onDestroy: abandon audio focus, unregister receiver, release session

package io.github.arrowplayer.app

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Build
import android.os.Bundle
import androidx.annotation.OptIn
import androidx.core.app.NotificationCompat
import androidx.media3.common.AudioAttributes as Media3AudioAttributes
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.session.MediaSession
import androidx.media3.session.MediaSessionService
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel

@OptIn(UnstableApi::class)
class PlaybackService : MediaSessionService() {

    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    private lateinit var mediaSession: MediaSession
    private lateinit var audioManager: AudioManager
    private lateinit var focusRequest: AudioFocusRequest
    private lateinit var sessionCallback: ArrowMediaSessionCallback

    private var audioFocusEnabled = false
    private var pausedForTransientLoss = false
    private var hasActiveQueue = false

    // -----------------------------------------------------------------------
    // Audio focus handling (REQ-OSI-041)
    // -----------------------------------------------------------------------
    private val audioFocusChangeListener = AudioManager.OnAudioFocusChangeListener { focusChange ->
        when (focusChange) {
            AudioManager.AUDIOFOCUS_LOSS -> {
                // Permanent loss — pause immediately, do not resume
                mediaSession.player.pause()
                pausedForTransientLoss = false
            }
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT -> {
                // Temporary loss — pause and mark for resume
                mediaSession.player.pause()
                pausedForTransientLoss = true
            }
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> {
                // Duck to 20% volume per REQ-OSI-041
                mediaSession.player.volume = 0.2f
            }
            AudioManager.AUDIOFOCUS_GAIN -> {
                // Restore full volume; resume if we paused for transient loss
                mediaSession.player.volume = 1.0f
                if (pausedForTransientLoss) {
                    mediaSession.player.play()
                    pausedForTransientLoss = false
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // REQ-OSI-042: pause when headphones / BT audio is disconnected
    // -----------------------------------------------------------------------
    private val becomingNoisyReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action == AudioManager.ACTION_AUDIO_BECOMING_NOISY) {
                mediaSession.player.pause()
            }
        }
    }

    override fun onCreate() {
        super.onCreate()

        audioManager = getSystemService(Context.AUDIO_SERVICE) as AudioManager

        // REQ-OSI-041: audio focus request with USAGE_MEDIA + CONTENT_TYPE_MUSIC
        val audioAttributes = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_MEDIA)
            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
            .build()

        focusRequest = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(audioAttributes)
            .setAcceptsDelayedFocusGain(true)
            .setOnAudioFocusChangeListener(audioFocusChangeListener)
            .build()

        // Build the player. The engine is in-process; the service holds the
        // Player reference that MediaSession wraps.
        val player = androidx.media3.exoplayer.ExoPlayer.Builder(this)
            .setAudioAttributes(
                Media3AudioAttributes.Builder()
                    .setUsage(C.USAGE_MEDIA)
                    .setContentType(C.AUDIO_CONTENT_TYPE_MUSIC)
                    .build(),
                /* handleAudioFocus = */ false, // we manage focus manually above
            )
            .setHandleAudioBecomingNoisy(false) // we handle it above
            .build()

        // Create the session callback that drives the player.
        sessionCallback = ArrowMediaSessionCallback(player)

        mediaSession = MediaSession.Builder(this, player)
            .setCallback(sessionCallback)
            .build()

        // REQ-OSI-042: register the becoming-noisy receiver
        val noisyFilter = IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(becomingNoisyReceiver, noisyFilter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(becomingNoisyReceiver, noisyFilter)
        }
    }

    override fun onGetSession(controllerInfo: MediaSession.ControllerInfo): MediaSession {
        return mediaSession
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // Handle incoming notification action intents.
        handleIntent(intent)

        // Request audio focus before starting playback
        val focusResult = audioManager.requestAudioFocus(focusRequest)
        audioFocusEnabled = (focusResult == AudioManager.AUDIOFOCUS_REQUEST_GRANTED)

        createNotificationChannel()
        val notification = buildNotification()
        startForeground(NOTIFICATION_ID, notification)

        // Check queue state for sticky decision
        hasActiveQueue = mediaSession.player.mediaItemCount > 0

        return if (hasActiveQueue) Service.START_STICKY else Service.START_NOT_STICKY
    }

    private fun handleIntent(intent: Intent?) {
        when (intent?.action) {
            ACTION_PAUSE -> mediaSession.player.pause()
            ACTION_PLAY -> mediaSession.player.play()
            ACTION_SKIP_PREV -> {
                val player = mediaSession.player
                if (player.currentPosition > 3000) {
                    player.seekTo(0)
                } else {
                    player.seekToPreviousMediaItem()
                }
            }
            ACTION_SKIP_NEXT -> mediaSession.player.seekToNextMediaItem()
            ACTION_STOP -> {
                mediaSession.player.stop()
                stopSelf()
            }
            ACTION_SEEK_TO -> {
                val position = intent.getLongExtra(EXTRA_POSITION, 0L)
                mediaSession.player.seekTo(position)
            }
        }
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        // Per spec: stop when paused without queue — not on every removal
        if (!mediaSession.player.isPlaying && mediaSession.player.mediaItemCount == 0) {
            stopSelf()
        }
        super.onTaskRemoved(rootIntent)
    }

    override fun onDestroy() {
        // REQ-OSI-042: unregister the becoming-noisy receiver
        try {
            unregisterReceiver(becomingNoisyReceiver)
        } catch (_: IllegalArgumentException) {
            // already unregistered
        }

        // Abandon audio focus
        if (audioFocusEnabled) {
            audioManager.abandonAudioFocusRequest(focusRequest)
        }

        // Release MediaSession and Player
        mediaSession.run {
            player.release()
            release()
        }

        // Cancel coroutine scope
        serviceScope.cancel()

        super.onDestroy()
    }

    // -----------------------------------------------------------------------
    // Notification channel (Android O+)
    // -----------------------------------------------------------------------
    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                getString(R.string.app_name),
                NotificationManager.IMPORTANCE_LOW,
            ).apply {
                description = "Playback controls"
                setShowBadge(false)
            }
            val nm = getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(channel)
        }
    }

    // -----------------------------------------------------------------------
    // REQ-UIX-027: MediaStyle notification with up to 5 actions
    // -----------------------------------------------------------------------
    private fun buildNotification(): Notification {
        val player = mediaSession.player
        val mediaMetadata = player.mediaMetadata

        // Content intent: launch app when notification body is tapped.
        val contentIntent = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )

        // Pending intent for launching app from notification (REQ-OSI-040).
        val launchIntent = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java).apply {
                flags = Intent.FLAG_ACTIVITY_SINGLE_TOP
            },
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )

        // Notification actions.
        val prevAction = NotificationCompat.Action(
            android.R.drawable.ic_media_previous,
            "Previous",
            PendingIntent.getService(
                this,
                REQUEST_CODE_SKIP_PREV,
                Intent(this, PlaybackService::class.java).apply {
                    action = ACTION_SKIP_PREV
                },
                PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
            ),
        )

        val isPlaying = player.isPlaying
        val playPauseAction = if (isPlaying) {
            NotificationCompat.Action(
                android.R.drawable.ic_media_pause,
                "Pause",
                PendingIntent.getService(
                    this,
                    REQUEST_CODE_PLAY_PAUSE,
                    Intent(this, PlaybackService::class.java).apply {
                        action = if (isPlaying) ACTION_PAUSE else ACTION_PLAY
                    },
                    PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
                ),
            )
        } else {
            NotificationCompat.Action(
                android.R.drawable.ic_media_play,
                "Play",
                PendingIntent.getService(
                    this,
                    REQUEST_CODE_PLAY_PAUSE,
                    Intent(this, PlaybackService::class.java).apply {
                        action = ACTION_PLAY
                    },
                    PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
                ),
            )
        }

        val nextAction = NotificationCompat.Action(
            android.R.drawable.ic_media_next,
            "Next",
            PendingIntent.getService(
                this,
                REQUEST_CODE_SKIP_NEXT,
                Intent(this, PlaybackService::class.java).apply {
                    action = ACTION_SKIP_NEXT
                },
                PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
            ),
        )

        val stopAction = NotificationCompat.Action(
            android.R.drawable.ic_menu_close_clear_cancel,
            "Stop",
            PendingIntent.getService(
                this,
                REQUEST_CODE_STOP,
                Intent(this, PlaybackService::class.java).apply {
                    action = ACTION_STOP
                },
                PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
            ),
        )

        // MediaStyle: compact view shows prev/play-pause/next in the compact area.
        val style = androidx.media.app.NotificationCompat.MediaStyle()
            .setShowActionsInCompactView(0, 1, 2) // prev, play/pause, next
            .setMediaSession(mediaSession.sessionCompatToken)
            .setShowCancelButton(true)
            .setCancelButtonIntent(
                PendingIntent.getService(
                    this,
                    REQUEST_CODE_STOP,
                    Intent(this, PlaybackService::class.java).apply {
                        action = ACTION_STOP
                    },
                    PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
                ),
            )

        val builder = NotificationCompat.Builder(this, CHANNEL_ID)

        // Set content metadata from MediaMetadata.
        val title = mediaMetadata.title?.toString() ?: getString(R.string.app_name)
        val artist = mediaMetadata.artist?.toString() ?: ""
        val album = mediaMetadata.albumTitle?.toString() ?: ""

        builder
            .setContentTitle(title)
            .setContentText(if (artist.isNotEmpty()) artist else album)
            .setSubText(album)
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentIntent(contentIntent)
            .setContentIntent(launchIntent)
            .setStyle(style)
            .addAction(prevAction)
            .addAction(playPauseAction)
            .addAction(nextAction)
            .addAction(stopAction)
            .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
            .setOngoing(isPlaying)
            .setShowWhen(false)
            .setOnlyAlertOnce(true)

        // Load album art asynchronously if available.
        val artUri = mediaMetadata.artworkUri
        if (artUri != null) {
            try {
                val bitmap = loadArtwork(artUri.toString())
                if (bitmap != null) {
                    builder.setLargeIcon(bitmap)
                }
            } catch (_: Exception) {
                // Artwork unavailable — build without it.
            }
        }

        // Expose playback position for the lock screen / media controls.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            builder.setPlaybackMetadata(
                NotificationCompat.METADATA_CONTENT_TYPE_MUSIC
            )
        }

        return builder.build()
    }

    /**
     * Loads artwork bitmap from a content URI.
     * This is a synchronous helper for the notification; in production this
     * should be replaced with a cached async loader (e.g. Coil / Glide).
     */
    private fun loadArtwork(uri: String): Bitmap? {
        return try {
            val inputStream = contentResolver.openInputStream(android.net.Uri.parse(uri))
            inputStream?.use { BitmapFactory.decodeStream(it) }
        } catch (_: Exception) {
            null
        }
    }

    companion object {
        private const val CHANNEL_ID = "arrow_player_playback"
        private const val NOTIFICATION_ID = 1

        private const val REQUEST_CODE_SKIP_PREV = 3
        private const val REQUEST_CODE_PLAY_PAUSE = 1
        private const val REQUEST_CODE_SKIP_NEXT = 4
        private const val REQUEST_CODE_STOP = 5

        const val ACTION_PAUSE = "io.github.arrowplayer.app.PAUSE"
        const val ACTION_PLAY = "io.github.arrowplayer.app.PLAY"
        const val ACTION_SKIP_PREV = "io.github.arrowplayer.app.SKIP_PREV"
        const val ACTION_SKIP_NEXT = "io.github.arrowplayer.app.SKIP_NEXT"
        const val ACTION_STOP = "io.github.arrowplayer.app.STOP"
        const val ACTION_SEEK_TO = "io.github.arrowplayer.app.SEEK_TO"
        const val EXTRA_POSITION = "position"
    }
}
