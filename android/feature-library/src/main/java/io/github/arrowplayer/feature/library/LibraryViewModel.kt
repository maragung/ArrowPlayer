// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Library view model — spec §7.1 layer 4 (APPLICATION), §12.1.
//
// Derives library browser state from the library repository.  During Phase 1
// the repository is a stub; this view model uses sample data to keep the UI
// exercised and ready for the real repository.

package io.github.arrowplayer.feature.library

import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

// ── UI models ───────────────────────────────────────────────────────────────

data class ArtistUiModel(
    val id: Long,
    val name: String,
    val albumCount: Int,
    val trackCount: Int,
    val artworkUrl: String? = null,
)

data class AlbumUiModel(
    val id: Long,
    val title: String,
    val artist: String,
    val year: Int? = null,
    val trackCount: Int = 0,
    val artworkUrl: String? = null,
)

data class SongUiModel(
    val id: Long,
    val title: String,
    val artist: String,
    val album: String,
    val trackNumber: Int? = null,
    val durationMs: Long = 0L,
    val durationStr: String = "",
    val playCount: Int = 0,
)

data class LibraryUiState(
    val artists: List<ArtistUiModel> = emptyList(),
    val albums: List<AlbumUiModel> = emptyList(),
    val songs: List<SongUiModel> = emptyList(),
    val searchQuery: String = "",
    val sortBy: String = "name",
    val isLoading: Boolean = false,
    val isEmpty: Boolean = true,
)

// ── View model ──────────────────────────────────────────────────────────────

class LibraryViewModel(
    private val context: Context,
) : ViewModel() {

    private val _uiState = MutableStateFlow(LibraryUiState())
    val uiState: StateFlow<LibraryUiState> = _uiState.asStateFlow()

    private var searchJob: Job? = null

    init {
        loadSampleData()
    }

    private fun loadSampleData() {
        // Phase 1: sample data keeps the UI exercised.  The real repository
        // (Phase 3) will replace this with a Flow<LibraryUiState> from the
        // library database, driven by the library scan service.
        val artists = listOf(
            ArtistUiModel(1, "Aphex Twin", 4, 38),
            ArtistUiModel(2, "Boards of Canada", 3, 29),
            ArtistUiModel(3, "Autechre", 5, 52),
            ArtistUiModel(4, "Squarepusher", 6, 61),
            ArtistUiModel(5, "Squarepusher", 6, 61),
        )
        val albums = listOf(
            AlbumUiModel(1, "Selected Ambient Works Vol. II", "Aphex Twin", 2014, 23),
            AlbumUiModel(2, "Music Has the Right to Children", "Boards of Canada", 1998, 12),
            AlbumUiModel(3, "Tri Repetae", "Autechre", 1995, 11),
            AlbumUiModel(4, "Come to Daddy", "Aphex Twin", 1997, 8),
            AlbumUiModel(5, "Geogaddi", "Boards of Canada", 2002, 22),
        )
        val songs = listOf(
            SongUiModel(1, "Xylem Tube", "Aphex Twin", "Selected Ambient Works Vol. II", 1, 10_000, "0:10"),
            SongUiModel(2, "Rhubarb", "Aphex Twin", "Selected Ambient Works Vol. II", 2, 300_000, "5:00"),
            SongUiModel(3, "High Pass", "Aphex Twin", "Selected Ambient Works Vol. II", 3, 420_000, "7:00"),
            SongUiModel(4, "Kid Phife", "Boards of Canada", "Music Has the Right to Children", 1, 210_000, "3:30"),
            SongUiModel(5, "Roygbiv", "Boards of Canada", "Music Has the Right to Children", 2, 180_000, "3:00"),
            SongUiModel(6, "Eeehh", "Autechre", "Tri Repetae", 1, 240_000, "4:00"),
            SongUiModel(7, "Nil", "Autechre", "Tri Repetae", 2, 195_000, "3:15"),
            SongUiModel(8, "Pin", "Autechre", "Tri Repetae", 3, 312_000, "5:12"),
        )

        _uiState.update {
            it.copy(
                artists = artists,
                albums = albums,
                songs = songs,
                isEmpty = false,
            )
        }
    }

    fun setSearchQuery(query: String) {
        _uiState.update { it.copy(searchQuery = query) }
        // Debounce: REQ-LIB-061 requires 120 ms debounce
        searchJob?.cancel()
        searchJob = viewModelScope.launch {
            delay(120L)
            performSearch(query)
        }
    }

    private fun performSearch(query: String) {
        if (query.isBlank()) {
            loadSampleData()
            return
        }
        val q = query.lowercase()
        _uiState.update { state ->
            state.copy(
                artists = state.artists.filter { it.name.lowercase().contains(q) },
                albums = state.albums.filter {
                    it.title.lowercase().contains(q) || it.artist.lowercase().contains(q)
                },
                songs = state.songs.filter {
                    it.title.lowercase().contains(q) || it.artist.lowercase().contains(q)
                },
                isEmpty = true,
            )
        }
    }

    fun setSortBy(field: String) {
        _uiState.update { state ->
            val sorted = when (field) {
                "name" -> state.copy(
                    artists = state.artists.sortedBy { it.name },
                    albums = state.albums.sortedBy { it.title },
                    songs = state.songs.sortedBy { it.title },
                    sortBy = field,
                )
                "artist" -> state.copy(
                    artists = state.artists.sortedBy { it.name },
                    albums = state.albums.sortedBy { it.artist },
                    songs = state.songs.sortedBy { it.artist },
                    sortBy = field,
                )
                else -> state.copy(sortBy = field)
            }
            sorted
        }
    }
}

class LibraryViewModelFactory(
    private val context: Context,
) : ViewModelProvider.Factory {
    @Suppress("UNCHECKED_CAST")
    override fun <T : ViewModel> create(modelClass: Class<T>): T {
        if (modelClass.isAssignableFrom(LibraryViewModel::class.java)) {
            return LibraryViewModel(context.applicationContext) as T
        }
        throw IllegalArgumentException("Unknown ViewModel class: ${modelClass.name}")
    }
}
