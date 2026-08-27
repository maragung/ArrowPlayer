// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Eclipse Player contributors
//
// Minimal Phase 0 unit test: the version the About screen shows (via
// BuildConfig.VERSION_NAME) must be the version the build declares. The real
// suite grows with the code it tests; this one exists so android-ci's
// testDebugUnitTest has something to run on day one, and so a scaffold that
// reports the wrong version is caught rather than shipped.

package io.github.eclipseplayer.app

import org.junit.Assert.assertEquals
import org.junit.Test

class BuildConfigTest {
    @Test
    fun versionNameMatchesTheDeclaredVersion() {
        // Mirrors gradle.properties' eclipse.version (the single Android-side
        // source, per ADR 0012).
        assertEquals("0.1.0", BuildConfig.VERSION_NAME)
    }

    @Test
    fun applicationIdIsStable() {
        assertEquals("io.github.eclipseplayer.app", BuildConfig.APPLICATION_ID)
    }
}
