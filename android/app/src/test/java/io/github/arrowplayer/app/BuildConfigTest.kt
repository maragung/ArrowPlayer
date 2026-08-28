// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Minimal Phase 0 unit test: the version the About screen shows (via
// BuildConfig.VERSION_NAME) must be the version the build declares. The real
// suite grows with the code it tests; this one exists so android-ci's
// testDebugUnitTest has something to run on day one, and so a scaffold that
// reports the wrong version is caught rather than shipped.

package io.github.arrowplayer.app

import org.junit.Assert.assertEquals
import org.junit.Test

class BuildConfigTest {
    @Test
    fun versionNameMatchesTheDeclaredVersion() {
        // Mirrors gradle.properties' arrow.version (the single Android-side
        // source, per ADR 0012). The release commit that bumps the version
        // for the next tag also has to keep this assertion in lockstep —
        // a unit test that pinned the previous value was the only thing
        // stopping android-ci from going green on a tag push. The
        // -PreleaseVersion CI override (REQ-BLD-002) does not change that
        // contract: when it is set, the value it stamps is also what this
        // assertion would have to read, and the gate fails rather than
        // producing a green run on a misconfigured version.
        assertEquals("0.3.0", BuildConfig.VERSION_NAME)
    }

    @Test
    fun applicationIdIsStable() {
        assertEquals("io.github.arrowplayer.app", BuildConfig.APPLICATION_ID)
    }
}
