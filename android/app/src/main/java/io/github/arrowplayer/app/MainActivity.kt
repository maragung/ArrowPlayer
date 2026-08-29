// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// MainActivity — Phase 1 scaffold (ADR 0012, §12.1).  Provides bottom navigation
// between the Now Playing and Library screens.  The navigation host lives here;
// individual screens are in their respective feature modules.

package io.github.arrowplayer.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.LibraryMusic
import androidx.compose.material.icons.filled.PlayCircle
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.navigation.NavDestination.Companion.hierarchy
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import io.github.arrowplayer.app.ui.theme.ArrowTheme
import io.github.arrowplayer.feature.library.LibraryScreen
import io.github.arrowplayer.feature.player.NowPlayingScreen

object NavRoutes {
    const val NOW_PLAYING = "now_playing"
    const val LIBRARY = "library"
}

data class BottomNavItem(
    val route: String,
    val label: Int,
    val icon: ImageVector,
)

@Composable
fun ArrowPlayerApp() {
    val navController = rememberNavController()

    val bottomNavItems = listOf(
        BottomNavItem(
            route = NavRoutes.NOW_PLAYING,
            label = R.string.now_playing,
            icon = Icons.Default.PlayCircle,
        ),
        BottomNavItem(
            route = NavRoutes.LIBRARY,
            label = R.string.library,
            icon = Icons.Default.LibraryMusic,
        ),
    )

    Scaffold(
        bottomBar = {
            NavigationBar {
                val navBackStackEntry by navController.currentBackStackEntryAsState()
                val currentDestination = navBackStackEntry?.destination

                bottomNavItems.forEach { item ->
                    NavigationBarItem(
                        icon = { Icon(item.icon, contentDescription = null) },
                        label = { Text(stringResource(item.label)) },
                        selected = currentDestination?.hierarchy?.any { it.route == item.route } == true,
                        onClick = {
                            navController.navigate(item.route) {
                                // Pop up to the start destination of the graph to
                                // avoid building up a large back stack.
                                popUpTo(navController.graph.findStartDestination().id) {
                                    saveState = true
                                }
                                launchSingleTop = true
                                restoreState = true
                            }
                        },
                    )
                }
            }
        },
    ) { innerPadding ->
        NavHost(
            navController = navController,
            startDestination = NavRoutes.NOW_PLAYING,
            modifier = Modifier.padding(innerPadding),
        ) {
            composable(NavRoutes.NOW_PLAYING) {
                NowPlayingScreen(
                    onNavigateToQueue = { /* queue screen — future phase */ },
                    onNavigateToLibrary = {
                        navController.navigate(NavRoutes.LIBRARY) {
                            popUpTo(navController.graph.findStartDestination().id) {
                                saveState = true
                            }
                            launchSingleTop = true
                            restoreState = true
                        }
                    },
                )
            }
            composable(NavRoutes.LIBRARY) {
                LibraryScreen(
                    onArtistClick = { /* artist detail — future phase */ },
                    onAlbumClick = { /* album detail — future phase */ },
                    onSongClick = { /* song detail — future phase */ },
                    onPlayArtist = { /* play artist — future phase */ },
                    onPlayAlbum = { /* play album — future phase */ },
                    onPlaySong = { /* play song — future phase */ },
                )
            }
        }
    }
}

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            ArrowTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background,
                ) {
                    ArrowPlayerApp()
                }
            }
        }
    }
}
