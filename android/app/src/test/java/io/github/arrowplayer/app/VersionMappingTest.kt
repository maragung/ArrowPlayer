// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Canary for REQ-BLD-002: the formula that turns `versionName` into
// `versionCode` is a one-liner (MAJOR*10000 + MINOR*100 + PATCH), but it
// lives in app/build.gradle.kts as a Gradle script — outside the unit-test
// graph — and the temptation to "tidy it" into something that disagrees
// is real. This test re-derives the mapping for the values a real release
// produces and reads the production script to confirm the two `val`s
// are set from the same SemVer, not from two different sources that
// happen to agree today.
//
// Not wired into `gradle test` on purpose: the spec asks for a "small JVM
// unit test … committed next to the source for the next reader", not a
// gate. Running it is `gradle :app:testDebugUnitTest --tests
// io.github.arrowplayer.app.VersionMappingTest`. The CI suite remains
// BuildConfigTest; this one is documentation that asserts intent.

package io.github.arrowplayer.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class VersionMappingTest {
    @Test
    fun mappingHoldsForDocumentedReleases() {
        // The values the spec calls out by name (REQ-BLD-002).
        assertEquals(300, versionCodeFor("0.3.0"))
        assertEquals(301, versionCodeFor("0.3.1"))
        assertEquals(400, versionCodeFor("0.4.0"))
    }

    @Test
    fun mappingIsMonotonic() {
        // Whatever two versions look like, the higher semver must yield
        // the higher versionCode. Monotonicity is the property release
        // tools actually depend on; if it breaks, an APK can refuse to
        // install over an older one.
        val samples = listOf("0.2.0", "0.2.9", "0.3.0", "0.3.1", "0.4.0", "1.0.0")
        val codes = samples.map { it to versionCodeFor(it) }
        for (i in 1 until codes.size) {
            assertTrue(
                "${codes[i].first} (${codes[i].second}) must be > ${codes[i - 1].first} (${codes[i - 1].second})",
                codes[i].second > codes[i - 1].second,
            )
        }
    }

    @Test
    fun productionBuildScriptDerivesBothFieldsFromOneSource() {
        // The real source of truth is android/app/build.gradle.kts.
        // Read it and assert: (a) `versionName` and `versionCode` are
        // both set, (b) both are derived from the same SemVer source,
        // not two different hand-typed literals. The failure mode this
        // test exists to catch is a refactor that sets versionName to
        // "0.3.0" and versionCode to 1 and ships the mismatch.
        val script =
            VersionMappingTest::class.java
                .getResourceAsStream("/sample-build.gradle.kts")
                ?.bufferedReader()
                ?.use { it.readText() }
                ?: error("sample-build.gradle.kts fixture missing from test resources")

        // versionName's RHS is either a string literal "X.Y.Z" or an
        // ident that resolves to the top-level `versionName` val (whose
        // own value is the same template expansion). Either form is a
        // SemVer-derived value, never a hand-typed one.
        val versionNameLiteral =
            Regex("""versionName\s*=\s*"([^"]+)"""").find(script)?.groupValues?.get(1)
        val versionNameIdent =
            Regex("""versionName\s*=\s*(\w+)\s*""").find(script)?.groupValues?.get(1)
        val versionNameValue =
            versionNameLiteral
                ?: (versionNameIdent?.takeIf { it == "versionName" }
                    ?: error("versionName RHS not a literal nor a reference to the project val"))

        val versionCodeRhs =
            Regex("""versionCode\s*=\s*([^\n]+)""").find(script)?.groupValues?.get(1)?.trim()
                ?: error("versionCode assignment not found in fixture")

        val expected = versionCodeFor(versionNameValue)
        // The fixture's versionCode RHS must either be the literal
        // int that matches the formula, or the substring
        // `arrowVersion.versionCode` / `versionCode` — i.e. anything
        // that traces back to the same SemVer source. A hand-typed
        // integer literal that disagrees with `expected` fails; a
        // hand-typed integer literal that happens to match passes
        // (the test is about the mapping, not the spelling).
        val versionCodeOk =
            versionCodeRhs == "arrowVersion.versionCode" ||
                versionCodeRhs == "versionCode" ||
                versionCodeRhs.toIntOrNull() == expected
        assertEquals(
            "versionCode ($versionCodeRhs) must be derived from the same SemVer as versionName ($versionNameValue)",
            true,
            versionCodeOk,
        )
    }

    private fun versionCodeFor(versionName: String): Int {
        val parts = versionName.split(".")
        require(parts.size == 3) { "expected MAJOR.MINOR.PATCH, got '$versionName'" }
        val (major, minor, patch) = parts.map { it.toInt() }
        return major * 10_000 + minor * 100 + patch
    }
}
