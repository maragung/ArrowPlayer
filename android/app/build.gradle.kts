// Arrow Player — Android app module (spec §5: "thin shell: DI wiring,
// navigation, manifest"). Phase 0 scaffold: a Compose About screen showing the
// git-derived version, mirroring the desktop exit gate 7 on this platform.

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.ktlint)
    alias(libs.plugins.detekt)
}

// -----------------------------------------------------------------------
//  Version derivation (REQ-BLD-002)
//
//  Default: `arrow.version` from gradle.properties (the Android-side
//  source-of-truth; ADR 0012). This is what local dev builds see and what
//  `versionName` reports when no release is being prepared.
//
//  Release: a CI build on a `v*.*.*` tag passes `-PreleaseVersion=<tag>`
//  (e.g. `-PreleaseVersion=v0.3.0`). That wins over gradle.properties, the
//  leading `v` is stripped, the result becomes `versionName`, and
//  `versionCode` is derived from the same string.
//
//  Formula for versionCode: MAJOR * 10000 + MINOR * 100 + PATCH
//  → 0.3.0 = 300, 0.3.1 = 301, 0.4.0 = 400. Picked over "1 + commits
//  since last tag" because the formula is (a) a pure function of the
//  version string, (b) deterministic across reruns and offline builds,
//  and (c) trivially unit-testable without a git history. The trade-off
//  (MAJOR ≥ 10 collides with the MINOR slot) is a non-issue for a
//  project still on 0.x.
// -----------------------------------------------------------------------

data class SemVer(val major: Int, val minor: Int, val patch: Int) {
    val versionCode: Int get() = major * 10_000 + minor * 100 + patch
}

fun parseSemVer(raw: String): SemVer {
    // Accept "v0.3.0" or "0.3.0"; reject anything else loudly so a CI
    // misconfiguration (e.g. pointing at a branch ref) fails the build
    // rather than silently stamping the APK with the wrong version.
    val stripped = raw.trim().removePrefix("v")
    val match = Regex("^(\\d+)\\.(\\d+)\\.(\\d+)(?:[-+].*)?$").matchEntire(stripped)
        ?: throw GradleException(
            "Invalid release version '$raw': expected 'vMAJOR.MINOR.PATCH' (e.g. v0.3.0). " +
                "Set it via -PreleaseVersion=... or 'arrow.version' in gradle.properties.",
        )
    return SemVer(
        major = match.groupValues[1].toInt(),
        minor = match.groupValues[2].toInt(),
        patch = match.groupValues[3].toInt(),
    )
}

// -PreleaseVersion=... wins; otherwise gradle.properties. Read the
// property through `findProperty` so the unset case doesn't explode.
val releaseVersionOverride: String? =
    (project.findProperty("releaseVersion") as String?)?.takeIf { it.isNotBlank() }

val arrowVersion: SemVer =
    parseSemVer(
        releaseVersionOverride
            ?: (project.property("arrow.version") as String),
    )

// REQ-BLD-002: surface the values the Android extension is about to
// read on the project itself, so `gradle :app:properties` lists them.
// AGP keeps versionName/versionCode inside the `android` extension,
// not on the raw project property bag, so a plain
// `gradle :app:properties | grep` would miss them otherwise. The local
// vals are read by the `android { ... }` block below; the
// `project.extra.set(...)` calls mirror them onto the project so
// `:app:properties` shows them under those exact names.
//
// Naming: the script-level vals are prefixed `derived` so the bare
// names `versionName` / `versionCode` keep resolving to the
// `DefaultConfig` receiver's own properties inside the
// `defaultConfig { ... }` block. Without the prefix, a line like
// `versionCode = versionCode` is a self-assignment of the AGP
// property (which is unset at that point) and the generated
// `BuildConfig.VERSION_CODE` ends up as -1. That is a compile-time
// silent failure: the build succeeds, the APK ships, and the
// About screen renders an empty version string — the exact
// regression VersionMappingTest exists to catch.
val derivedVersionName: String = "${arrowVersion.major}.${arrowVersion.minor}.${arrowVersion.patch}"
val derivedVersionCode: Int = arrowVersion.versionCode

android {
    namespace = "io.github.arrowplayer.app"
    compileSdk = 35

    defaultConfig {
        applicationId = "io.github.arrowplayer.app"
        minSdk = 26
        targetSdk = 35
        // REQ-BLD-002: both fields are derived from the same source so
        // they cannot disagree. The RHS references the script-level
        // `derived*` vals; the LHS is the `DefaultConfig` property the
        // AGP reads when it generates BuildConfig.
        versionCode = derivedVersionCode
        versionName = derivedVersionName
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    // REQ-BLD-026: ship one APK per supported ABI rather than a single
    // universal APK. AGP names the per-ABI artifacts as
    // app-<abi>-release.apk (see developer.android.com/build/configure-apk-splits),
    // which is the pattern the android-ci upload step globs for.
    splits {
        abi {
            isEnable = true
            reset()
            include("arm64-v8a", "armeabi-v7a", "x86_64")
            isUniversalApk = false
        }
    }

    buildTypes {
        release {
            // No signing config yet — release artifacts are unsigned until a
            // signing identity exists (REQ-SEC-016, Phase 9). Stated, not
            // glossed: android-ci.yml says so in its release notes too.
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        compose = true
        // BuildConfig.VERSION_NAME is what the About screen shows; make the
        // generation explicit rather than relying on a default that varies.
        buildConfig = true
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.ui)
    implementation(libs.androidx.ui.graphics)
    implementation(libs.androidx.ui.tooling.preview)
    implementation(libs.androidx.material3)
    // Material Components for Android supplies the XML theme parents
    // Theme.Material3.DayNight.* that the activity theme in
    // res/values/themes.xml extends. Compose's `material3` artifact
    // ships the in-Compose theme engine only — it does NOT ship the
    // XML resources a <style parent="..."> lookup resolves against.
    // Without this dependency the manifest merger fails the build
    // with "resource style/Theme.Material3.DayNight.NoActionBar not
    // found" the first time the activity is inflated.
    implementation(libs.google.android.material)
    // REQ-OSI-040 / REQ-AUT-002: MediaLibraryService + MediaSession
    // (Media3). Pinned in the catalog (REQ-SEC-013) so the version
    // the subagent 11 service code lands on matches the one the
    // androidTest surface here exercises. media3-session transitively
    // pulls media3-common; we declare both so the test sources do
    // not need a `common` import that would otherwise be implicit.
    implementation(libs.androidx.media3.session)
    implementation(libs.androidx.media3.session.ktx)
    implementation(libs.androidx.media3.browse)
    implementation(libs.androidx.media3.exoplayer)
    implementation(libs.androidx.media3.common)
    // REQ-UIX-027: NotificationCompat.MediaStyle requires androidx.media
    implementation(libs.androidx.media)
    // REQ-AUT-001: ListenableFuture for MediaLibraryService async results
    implementation(libs.guava)
    testImplementation(libs.junit)
    // REQ-OSI-041 / REQ-OSI-042: the audio-focus matrix and the
    // becoming-noisy broadcast path are pure JVM-side contract
    // assertions on platform classes (AudioManager,
    // AudioFocusRequest, Intent.ACTION_AUDIO_BECOMING_NOISY). They
    // run on the host JVM via Robolectric, NOT on a device. Espresso
    // lives under androidTestImplementation only — the two runners
    // do not overlap so a single test never has both available.
    testImplementation(libs.robolectric)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    // MainActivityEspressoTest renders the About Compose screen and
    // asserts on its semantic text. Espresso's onView() only sees
    // Android Views — Compose nodes are reachable through
    // `composeTestRule`, which lives in `ui-test-junit4` (version
    // managed by the compose-bom above). `ui-test-manifest` is
    // intentionally NOT pulled in here: that artifact provides an
    // `androidx.activity.ComponentActivity` placeholder for tests
    // that do not have an activity of their own, and
    // `createAndroidComposeRule<MainActivity>()` uses our real
    // activity — the placeholder would be dead weight on the
    // classpath.
    androidTestImplementation(libs.androidx.ui.test.junit4)
    debugImplementation(libs.androidx.ui.tooling)
}

// REQ-BLD-002: mirror versionName/versionCode onto `project.extra` so
// `gradle :app:properties` reports them. The local vals above feed the
// `android { ... }` block; the project extra is a separate bag that
// `properties` enumerates. Same values, two surfaces — the property
// bag listing is what the spec's verification step greps for.
project.extra.set("versionName", derivedVersionName)
project.extra.set("versionCode", derivedVersionCode)
