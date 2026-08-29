// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Player view model — spec §7.1 layer 4 (APPLICATION), §12.1.
//
// Derives UI state from a Media3 Player instance.  Acts as the bridge between
// the Compose NowPlayingScreen (layer 5) and the PlaybackService / MediaSession
// (layer 1 adapter).  No business logic lives here; all state flows through.

package io.github.arrowplayer.feature.player

import android.content.ComponentName
import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.common.Player
import androidx.media3.session.MediaController
import androidx.media3.session.SessionToken
import com.google.common.util.concurrent.ListenableFuture
import com.google.common.util.concurrent.MoreExecutors
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/** UI state surfaced by the Now Playing screen. */
data class PlayerUiState(
    val title: String = "",
    val artist: String = "",
    val album: String = "",
    val artworkUrl: String? = null,
    val positionMs: Long = 0L,
    val durationMs: Long = 0L,
    val isPlaying: Boolean = false,
    val volume: Float = 1.0f,
    val repeatMode: Int = Player.REPEAT_MODE_OFF,
    val shuffleEnabled: Boolean = false,
    val isLoading: Boolean = false,
)

/** Player view model — derives UI state from a Media3 Player. */
class PlayerViewModel(
    private val context: Context,
) : ViewModel() {

    private val _uiState = MutableStateFlow(PlayerUiState())
    val uiState: StateFlow<PlayerUiState> = _uiState.asStateFlow()

    private var controllerFuture: ListenableFuture<MediaController>? = null
    private var mediaController: MediaController? = null
    private var positionUpdateJob: Job? = null

    private val playerListener = object : Player.Listener {
        override fun onIsPlayingChanged(playing: Boolean) {
            _uiState.update { it.copy(isPlaying = playing) }
            if (playing) {
                startPositionUpdates()
            } else {
                stopPositionUpdates()
            }
        }

        override fun onPlaybackStateChanged(state: Int) {
            _uiState.update {
                it.copy(isLoading = state == Player.STATE_BUFFERING)
            }
        }

        override fun onMediaItemTransition(mediaItem: MediaItem?, reason: Int) {
            mediaItem?.let { updateFromMediaItem(it) }
        }

        override fun onRepeatModeChanged(repeatMode: Int) {
            _uiState.update { it.copy(repeatMode = repeatMode) }
        }

        override fun onShuffleModeEnabledChanged(shuffleModeEnabled: Boolean) {
            _uiState.update { it.copy(shuffleEnabled = shuffleModeEnabled) }
        }

        override fun onVolumeChanged(volume: Float) {
            _uiState.update { it.copy(volume = volume) }
        }
    }

    init {
        connectToController()
    }

    private fun connectToController() {
        val sessionToken = SessionToken(
            context,
            ComponentName(context, "io.github.arrowplayer.app.PlaybackService"),
        )
        controllerFuture = MediaController.Builder(context, sessionToken).buildAsync()
        controllerFuture?.addListener({
            try {
                mediaController = controllerFuture?.get()
                mediaController?.addListener(playerListener)
                // Initial state
                mediaController?.currentMediaItem?.let { updateFromMediaItem(it) }
                _uiState.update {
                    it.copy(
                        positionMs = mediaController?.currentPosition ?: 0L,
                        durationMs = mediaController?.duration?.coerceAtLeast(0L) ?: 0L,
                        isPlaying = mediaController?.isPlaying ?: false,
                        volume = mediaController?.volume ?: 1.0f,
                        repeatMode = mediaController?.repeatMode ?: Player.REPEAT_MODE_OFF,
                        shuffleEnabled = mediaController?.shuffleModeEnabled ?: false,
                    )
                }
                if (mediaController?.isPlaying == true) {
                    startPositionUpdates()
                }
            } catch (_: Exception) {
                // Service not running yet; retry on next connect
            }
        }, MoreExecutors.directExecutor())
    }

    private fun updateFromMediaItem(mediaItem: MediaItem) {
        val meta = mediaItem.mediaMetadata
        val artwork = meta.artworkUri?.toString()
        _uiState.update {
            it.copy(
                title = meta.title?.toString() ?: "",
                artist = meta.artist?.toString() ?: "",
                album = meta.albumTitle?.toString() ?: "",
                artworkUrl = artwork,
                durationMs = mediaItem.mediaMetadata.extras?.getLong("durationMs")
                             ?: mediaController?.duration?.coerceAtLeast(0L) ?: 0L,
            )
        }
    }

    private fun startPositionUpdates() {
        positionUpdateJob?.cancel()
        positionUpdateJob = viewModelScope.launch {
            while (isActive) {
                mediaController?.let { ctrl ->
                    _uiState.update {
                        it.copy(positionMs = ctrl.currentPosition.coerceAtLeast(0L))
                    }
                }
                delay(250L)
            }
        }
    }

    private fun stopPositionUpdates() {
        positionUpdateJob?.cancel()
        positionUpdateJob = null
    }

    // ── Player actions ────────────────────────────────────────────────────────

    fun togglePlayPause() {
        mediaController?.let { ctrl ->
            if (ctrl.isPlaying) ctrl.pause() else ctrl.play()
        }
    }

    fun seekTo(positionMs: Long) {
        mediaController?.seekTo(positionMs.coerceAtLeast(0L))
    }

    fun skipToNext() {
        mediaController?.seekToNextMediaItem()
    }

    fun skipToPrevious() {
        mediaController?.seekToPreviousMediaItem()
    }

    fun toggleRepeat() {
        mediaController?.let { ctrl ->
            ctrl.repeatMode = when (ctrl.repeatMode) {
                Player.REPEAT_MODE_OFF -> Player.REPEAT_MODE_ALL
                Player.REPEAT_MODE_ALL -> Player.REPEAT_MODE_ONE
                else -> Player.REPEAT_MODE_OFF
            }
        }
    }

    fun toggleShuffle() {
        mediaController?.let { ctrl ->
            ctrl.shuffleModeEnabled = !ctrl.shuffleModeEnabled
        }
    }

    fun setVolume(volume: Float) {
        mediaController?.volume = volume.coerceIn(0f, 1f)
    }

    override fun onCleared() {
        stopPositionUpdates()
        mediaController?.removeListener(playerListener)
        controllerFuture?.let { MediaController.releaseFuture(it) }
        super.onCleared()
    }
}

/** Factory for PlayerViewModel that takes a Context. */
class PlayerViewModelFactory(
    private val context: Context,
) : ViewModelProvider.Factory {
    @Suppress("UNCHECKED_CAST")
    override fun <T : ViewModel> create(modelClass: Class<T>): T {
        if (modelClass.isAssignableFrom(PlayerViewModel::class.java)) {
            return PlayerViewModel(context.applicationContext) as T
        }
        throw IllegalArgumentException("Unknown ViewModel class: ${modelClass.name}")
    }
}
