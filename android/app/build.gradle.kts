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

// The single source of the Android-side version (gradle.properties).
val arrowVersion: String = (project.property("arrow.version") as String)

android {
    namespace = "io.github.arrowplayer.app"
    compileSdk = 35

    defaultConfig {
        applicationId = "io.github.arrowplayer.app"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = arrowVersion
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
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    debugImplementation(libs.androidx.ui.tooling)
}
