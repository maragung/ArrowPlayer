# ProGuard/R8 rules for Arrow Player — Android.
#
# Minification is off for the Phase 0 scaffold (see app/build.gradle.kts), so
# this file is the placeholder §5's layout requires. Rules arrive with the
# first code that needs them — Media3, TagLib NDK bindings, the skin engine —
# not before. The rule below is a placeholder that pins the app's own classes
# so the first phase that flips isMinifyEnabled to true does not have to
# re-derive them. The app package is one of the keepRoots the manifest
# merger already protects, but spelling it out here makes the contract
# explicit and survives a future AGP change that lifts the implicit
# protection (e.g. moving to R8 full-mode on the application class).

# Keep the app's own classes (and members) by name. The Phase 0
# scaffold has no reflection, no JNI, and no service binders, so a
# blanket keep is correct; it gets narrowed in a later phase when the
# first reflective consumer (Media3 session callbacks, skin engine
# loaders) lands and its specific keep rules are added below this one.
-keep class io.github.arrowplayer.app.** { *; }
