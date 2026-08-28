// Arrow Player — Android build root (spec §5: `android/` is a self-contained
// Gradle build; it never imports desktop/ code — REQ-GEN-030, §5 isolation).
//
// Version catalog lives in gradle/libs.versions.toml. No inline versions
// anywhere in module build files (§5 layout requirement).

pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    // §5: modules resolve through the catalog only.
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "ArrowPlayer"
include(":app")
