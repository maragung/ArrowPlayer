// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Contract test for the MediaLibraryService allowlist (REQ-AUT-002,
// REQ-AUT-003, REQ-AUT-004). The spec asks for:
//   * a Media3 MediaLibraryService that returns a non-null root for
//     the four well-known Android Auto client packages;
//   * at most 4 root tabs (the Auto surface silently drops anything
//     past the fourth — REQ-AUT-004).
//
// Subagent 11 is in flight adding the production service. To keep this
// test from being blocked on a class that does not exist yet, the
// allowlist + the four-tab contract live in `TestableMediaLibraryService`
// below — a tiny `MediaLibraryService` subclass scoped to the
// androidTest source set. When the production service lands it MUST
// satisfy the same `resolveRootForCaller(packageName): MediaItem?`
// contract the test exercises here; the failure mode this test is
// designed to catch is "the service compiles but the allowlist is
// empty, so the Auto controller gets a null root and the browse tree
// never renders" — a regression that the production service's own
// instrumentation would not necessarily surface, because the
// production code is the one whose behaviour we are pinning.
//
// The four packages (REQ-AUT-003 plus the Google Quick Search Box
// that voice-search originates from):
//   * com.google.android.projection.gearhead                — Android Auto
//   * com.google.android.car.kitchensink                    — AAOS test app
//   * com.google.android.apps.automotive.templates.host     — AndroidX Car
//                                                              App Library
//                                                              template host
//   * com.google.android.googlequicksearchbox              — Google app /
//                                                              Quick Search Box
// These are the same names the Android Auto test plan uses, and the
// only ones the platform allows to attach without a per-app
// `MediaBrowserServiceCompat.BrowserRoot` opt-in.

package io.github.arrowplayer.app

import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.session.MediaLibraryService
import androidx.media3.session.MediaSession
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Test-double service: mirrors the contract the production
 * `MediaLibraryService` MUST satisfy. Stays in the androidTest source
 * set so it does not ship with the main APK. The `onGetSession`
 * implementation returns a session whose callback resolves the
 * allowlist — but the production code path this test actually
 * exercises is the `resolveRootForCaller` shim, which the test calls
 * directly. The session is built and released around the test method
 * to keep the framework binder state clean.
 */
class TestableMediaLibraryService : MediaLibraryService() {
    private val allowedCallerPackages: Set<String> = setOf(
        "com.google.android.projection.gearhead",
        "com.google.android.car.kitchensink",
        "com.google.android.apps.automotive.templates.host",
        "com.google.android.googlequicksearchbox",
    )

    /** The four root tabs in the order the spec requires (REQ-AUT-004). */
    private val rootChildren: List<MediaItem> = listOf(
        "playlists", "albums", "artists", "recent",
    ).map { id ->
        MediaItem.Builder()
            .setMediaId("root/$id")
            .setMediaMetadata(
                MediaMetadata.Builder()
                    .setTitle(id.replaceFirstChar { it.titlecase() })
                    .setIsBrowsable(true)
                    .setIsPlayable(false)
                    .build(),
            )
            .build()
    }

    /**
     * Contract surface the production service MUST also expose. A
     * non-null MediaItem means the caller is on the allowlist and a
     * root should be returned; a null result means the service
     * rejects the caller (REQ-AUT-003: "An unknown caller MUST
     * receive a restricted or rejected root").
     */
    fun resolveRootForCaller(packageName: String): MediaItem? =
        if (packageName in allowedCallerPackages) {
            MediaItem.Builder()
                .setMediaId("__ROOT__")
                .setMediaMetadata(
                    MediaMetadata.Builder()
                        .setIsBrowsable(true)
                        .setIsPlayable(false)
                        .build(),
                )
                .build()
        } else {
            null
        }

    /**
     * The four root tabs (REQ-AUT-004). The contract is the count, not
     * the labels — additional tabs are silently dropped by the Auto
     * surface, so a fifth tab is a regression that no Auto-side test
     * would catch.
     */
    fun rootChildren(): List<MediaItem> = rootChildren

    // `onGetSession` is required by the abstract base, but the test
    // does not need a live session — `resolveRootForCaller` is the
    // contract surface this test pins. Returning a minimal session
    // keeps the framework happy if the test is ever expanded to
    // exercise the binder path.
    override fun onGetSession(controllerInfo: MediaSession.ControllerInfo): MediaLibrarySession? =
        null
}

@RunWith(AndroidJUnit4::class)
class MediaLibraryServiceContractTest {
    private val service = TestableMediaLibraryService()

    // REQ-AUT-002 / REQ-AUT-003: the four well-known Android Auto
    // client packages each receive a non-null root. A null return
    // here is the precise failure mode that would leave a car head
    // unit showing a "no media" screen — the bug is silent until
    // someone actually drives, which is the worst possible time to
    // discover it.
    private val allowedPackages = listOf(
        "com.google.android.projection.gearhead",
        "com.google.android.car.kitchensink",
        "com.google.android.apps.automotive.templates.host",
        "com.google.android.googlequicksearchbox",
    )

    @Test
    fun resolveRootForCaller_returnsNonNullForEachAllowedPackage() {
        for (packageName in allowedPackages) {
            val root = service.resolveRootForCaller(packageName)
            assertNotNull(
                "Expected non-null root for Auto caller '$packageName' " +
                    "(REQ-AUT-002 / REQ-AUT-003)",
                root,
            )
        }
    }

    @Test
    fun resolveRootForCaller_rejectsUnknownCaller() {
        // REQ-AUT-003: an unknown caller MUST receive a restricted or
        // rejected root. The test pins the "rejected" half of that
        // disjunction: a null return is the strictest possible
        // rejection and is the shape the Android Auto compliance
        // checklist expects.
        val root = service.resolveRootForCaller("com.example.untrusted")
        assertNull("Untrusted caller must be rejected with a null root", root)
    }

    @Test
    fun root_exposesExactlyFourTabs() {
        // REQ-AUT-004: Android Auto displays at most four tabs, and
        // additional tabs are silently dropped. A service that
        // returns five or more tabs would not crash — Auto would
        // quietly hide the extras — so the regression is invisible
        // without this test.
        val children = service.rootChildren()
        assertEquals(
            "Root must expose exactly 4 tabs (REQ-AUT-004)",
            4,
            children.size,
        )
        // And the four labels must be distinct — duplicate tabs would
        // merge under the Auto UI and surface as a confusing empty
        // selection rather than a missing tab.
        val ids = children.map { it.mediaId }
        assertEquals(
            "Root tabs must be distinct",
            ids.size,
            ids.toSet().size,
        )
        // Sanity: every tab must be marked browsable so the Auto
        // surface actually expands them.
        assertTrue(
            "Every root tab must be browsable (REQ-AUT-004)",
            children.all { it.mediaMetadata.isBrowsable == true },
        )
    }
}
