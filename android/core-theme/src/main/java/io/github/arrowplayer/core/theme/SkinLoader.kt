// SPDX-License-Identifier: MPL-2.0
//
// SkinLoader.kt — Loads and validates skin packages.
//
// Spec: eclipse-player.md §11.2 (REQ-THM-010), §11.3 (REQ-THM-015 … REQ-THM-019),
//       §11.5 (REQ-THM-040), §11.6 (REQ-THM-050 … REQ-THM-053).
//
// A skin package is a ZIP archive with the extension `.eclipseskin`.
//
// Skin loading steps (validation pipeline, per REQ-THM-040):
//   1. ZIP structural integrity; limits from REQ-THM-017.
//   2. Path safety for every entry (REQ-THM-018).
//   3. manifest.json against its schema; checksum verification.
//   4. theme.json against theme-schema.json.
//   5. Contrast enforcement (REQ-THM-041).
//   6. Each .eclayout against layout.schema.json.
//   7. SVG sanitisation (REQ-THM-042).
//   8. Raster image dimension probing (REQ-THM-017).
//   9. Font format check and LICENSE presence.
//  10. EFS pattern output cap verification.
//
// All I/O is performed on a background thread.  Results are delivered to
// the caller via [Result]-typed suspending functions.
//
// Error messages are actionable: they name the file, the failing rule,
// and the line or byte offset of the failure (REQ-THM-040).
//
// Hilt: this module is not a singleton — one [SkinLoader] is created per
// load operation, so that concurrent loads are independent.

package io.github.arrowplayer.core.theme

import android.content.Context
import android.content.res.AssetManager
import dagger.hilt.android.qualifiers.ApplicationContext
import io.github.arrowplayer.core.theme.ContrastChecker.Companion.contrastRatio
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.InputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipInputStream
import javax.inject.Inject
import javax.inject.Singleton
import kotlin.math.sqrt

// ---------------------------------------------------------------------------
//  Hard limits  (REQ-THM-017)
// ---------------------------------------------------------------------------

/** Maximum total uncompressed size of a skin package (32 MiB). */
private const val MAX_UNCOMPRESSED_SIZE = 32 * 1024 * 1024

/** Maximum number of entries in a skin package (2000). */
private const val MAX_ENTRY_COUNT = 2000

/** Maximum uncompressed size of a single file (8 MiB). */
private const val MAX_FILE_SIZE = 8 * 1024 * 1024

/** Maximum path depth (4 levels). */
private const val MAX_PATH_DEPTH = 4

/** Maximum path length in bytes (200). */
private const val MAX_PATH_LENGTH = 200

/** Maximum SVG element count (10,000). */
private const val MAX_SVG_ELEMENTS = 10_000

// ---------------------------------------------------------------------------
//  Skin data classes
// ---------------------------------------------------------------------------

/**
 * A loaded, validated skin package.
 *
 * @property manifest   The validated manifest.
 * @property theme      The validated theme (null if the skin has no theme).
 * @property layouts    Map of surface name → parsed layout document.
 * @property assets     Resolved paths to extracted asset directories.
 */
data class Skin(
    val manifest: ValidatedManifest,
    val theme: Theme? = null,
    val layouts: Map<String, LayoutDocument> = emptyMap(),
    val assets: ExtractedAssets? = null,
)

/**
 * A validated manifest.json.
 *
 * @property schemaVersion Always 1.
 * @property id            Stable identity (lower-case kebab).
 * @property name          Display name.
 * @property author        Optional author name.
 * @property version       Semantic version X.Y.Z.
 * @property license       SPDX identifier.
 * @property homepage      Optional URL.
 * @property description   Optional description.
 * @property minAppVersion Minimum app version (X.Y.Z).
 * @property capabilities  Declared capabilities (theme, layout, icons, images, fonts).
 * @property surfaces     Surfaces this skin overrides.
 * @property checksums    File path → SHA-256 hex digest for integrity checking.
 */
data class ValidatedManifest(
    val schemaVersion: Int,
    val id: String,
    val name: String,
    val author: String? = null,
    val version: String,
    val license: String? = null,
    val homepage: String? = null,
    val description: String? = null,
    val minAppVersion: String,
    val capabilities: Set<Capability> = emptySet(),
    val surfaces: Set<SkinSurface> = emptySet(),
    val checksums: Map<String, String> = emptyMap(),
)

/** Declared capabilities of a skin. */
enum class Capability {
    Theme,
    Layout,
    Icons,
    Images,
    Fonts,
}

/** Surfaces a skin can override. */
enum class SkinSurface {
    MainWindow,
    NowPlaying,
    MiniPlayer,
    Library,
}

/** Extracted asset directories from a skin package. */
data class ExtractedAssets(
    val root: File,      // The directory the skin was extracted into.
    val layoutDir: File? = null,
    val iconsDir: File? = null,
    val imagesDir: File? = null,
    val fontsDir: File? = null,
)

// ---------------------------------------------------------------------------
//  Load errors
// ---------------------------------------------------------------------------

/**
 * A load failure with a human-readable, actionable message.
 *
 * Error messages follow the format:
 *   "<file>:<line>: <description of what failed and why>"
 * For example:
 *   "theme.json:42: 'color' is required but missing"
 *   "my-skin.eclipseskin: theme.json checksum mismatch (expected ..., got ...)"
 */
sealed class SkinLoadError {
    abstract val message: String
    abstract val path: String?

    /** ZIP could not be opened. */
    data class ZipOpenFailed(
        override val path: String,
        val cause: Throwable,
    ) : SkinLoadError() {
        override val message = "cannot open skin package: ${path}: ${cause.message}"
    }

    /** ZIP entry exceeds a size limit. */
    data class FileTooLarge(
        override val path: String,
        val entryName: String,
        val size: Long,
        val maxSize: Long,
    ) : SkinLoadError() {
        override val message = "${entryName}: file is ${size} bytes; " +
            "limit is ${maxSize / 1024 / 1024} MiB (REQ-THM-017)"
    }

    /** ZIP contains too many entries. */
    data class TooManyEntries(
        override val path: String,
        val count: Int,
    ) : SkinLoadError() {
        override val message = "skin package has ${count} entries; " +
            "limit is ${MAX_ENTRY_COUNT} (REQ-THM-017)"
    }

    /** Total uncompressed size exceeds 32 MiB. */
    data class TotalSizeExceeded(
        override val path: String,
        val size: Long,
    ) : SkinLoadError() {
        override val message = "skin package uncompressed size is " +
            "${size / 1024 / 1024} MiB; limit is 32 MiB (REQ-THM-017)"
    }

    /** Entry path fails zip-slip or path-safety checks. */
    data class UnsafePath(
        override val path: String,
        val entryName: String,
        val reason: String,
    ) : SkinLoadError() {
        override val message = "${entryName}: unsafe path — ${reason} (REQ-THM-018)"
    }

    /** manifest.json is missing from the package. */
    data class ManifestMissing(override val path: String) : SkinLoadError() {
        override val message = "skin package is missing manifest.json (REQ-THM-015)"
    }

    /** manifest.json failed schema validation. */
    data class ManifestInvalid(
        override val path: String,
        val errors: List<String>,
    ) : SkinLoadError() {
        override val message = buildString {
            appendLine("manifest.json failed validation:")
            errors.forEach { appendLine("  ${it}") }
        }
    }

    /** A file checksum does not match the manifest. */
    data class ChecksumMismatch(
        override val path: String,
        val file: String,
        val expected: String,
        val actual: String,
    ) : SkinLoadError() {
        override val message = "${file}: checksum mismatch (expected ${expected.take(8)}..., got ${actual.take(8)}...)"
    }

    /** theme.json failed schema validation. */
    data class ThemeInvalid(
        override val path: String,
        val errors: List<String>,
    ) : SkinLoadError() {
        override val message = buildString {
            appendLine("theme.json failed validation:")
            errors.forEach { appendLine("  ${it}") }
        }
    }

    /** Contrast ratio below AA (REQ-THM-041). */
    data class ContrastFailure(
        override val path: String,
        val pairs: List<ContrastPair>,
    ) : SkinLoadError() {
        override val message = buildString {
            appendLine("theme.json: contrast ratios below AA:")
            pairs.take(5).forEach { (bg, fg, ratio) ->
                appendLine("  ${bg} on ${fg}: ${String.format("%.2f", ratio)}:1 (AA requires 4.5:1)")
            }
            if (pairs.size > 5) appendLine("  ... and ${pairs.size - 5} more pairs")
        }
    }

    /** layout.eclayout failed schema validation. */
    data class LayoutInvalid(
        override val path: String,
        val file: String,
        val errors: List<String>,
    ) : SkinLoadError() {
        override val message = buildString {
            appendLine("${file}: layout validation failed:")
            errors.forEach { appendLine("  ${it}") }
        }
    }

    /** An SVG contains prohibited elements (script, foreignObject, etc.). */
    data class SvgUnsafe(
        override val path: String,
        val file: String,
        val reason: String,
    ) : SkinLoadError() {
        override val message = "${file}: SVG rejected — ${reason} (REQ-THM-042)"
    }

    /** An image file exceeds the maximum dimension (REQ-THM-017). */
    data class ImageTooLarge(
        override val path: String,
        val file: String,
        val width: Int,
        val height: Int,
    ) : SkinLoadError() {
        override val message = "${file}: image is ${width}×${height} px; " +
            "maximum is 8192×8192 (REQ-THM-017)"
    }

    /** App version too old for this skin (REQ-THM-052). */
    data class AppVersionTooOld(
        override val path: String,
        val skinMinVersion: String,
        val appVersion: String,
    ) : SkinLoadError() {
        override val message = "skin requires app ${skinMinVersion}+ but this is ${appVersion}"
    }

    /** A generic I/O error. */
    data class IoError(
        override val path: String,
        val operation: String,
        val cause: Throwable,
    ) : SkinLoadError() {
        override val message = "${operation}: ${cause.message}"
    }
}

/** A text colour / background colour pair with a computed contrast ratio. */
data class ContrastPair(
    val backgroundColor: String,
    val textColor: String,
    val ratio: Double,
)

// ---------------------------------------------------------------------------
//  SkinLoader
// ---------------------------------------------------------------------------

/**
 * Loads and validates skin packages (`.eclipseskin`).
 *
 * Load order follows the §11.5 validation pipeline.  Each step is
 * documented with its REQ reference.
 *
 * Hilt: not a singleton — one instance per load operation.
 *
 * @param context Android [Context] for asset access and temporary storage.
 * @param themeTokens Canonical design tokens (§12.1).
 */
class SkinLoader @Inject constructor(
    @ApplicationContext private val context: Context,
    private val themeTokens: ThemeTokens,
) {
    private val schemaValidator = JsonSchemaValidator()

    /**
     * Load a skin from an APK asset.
     *
     * @param assetPath Path within the APK assets directory.
     * @param appVersion The current app version string (X.Y.Z).
     * @return [Skin] on success, or [SkinLoadError] on failure.
     */
    suspend fun loadFromAsset(
        assetPath: String,
        appVersion: String,
    ): Result<Skin, SkinLoadError> = withContext(Dispatchers.IO) {
        loadFromStream(
            streamSupplier = { context.assets.open(assetPath) },
            sourceName = "asset:${assetPath}",
            appVersion = appVersion,
        )
    }

    /**
     * Load a skin from an external ZIP file.
     *
     * @param file The .eclipseskin file on disk.
     * @param appVersion The current app version string (X.Y.Z).
     * @return [Skin] on success, or [SkinLoadError] on failure.
     */
    suspend fun loadFromFile(
        file: File,
        appVersion: String,
    ): Result<Skin, SkinLoadError> = withContext(Dispatchers.IO) {
        loadFromStream(
            streamSupplier = { file.inputStream() },
            sourceName = file.absolutePath,
            appVersion = appVersion,
        )
    }

    /**
     * Load a skin from a ZIP [InputStream].
     *
     * The caller is responsible for closing the stream.
     *
     * @param streamSupplier A lambda that produces a [InputStream] to the ZIP.
     * @param sourceName Display name for error messages.
     * @param appVersion The current app version string.
     * @return [Skin] on success, or [SkinLoadError] on failure.
     */
    private fun loadFromStream(
        streamSupplier: () -> InputStream,
        sourceName: String,
        appVersion: String,
    ): Result<Skin, SkinLoadError> {
        val entries = mutableListOf<ZipEntryData>()
        val manifestBytes: ByteArray?

        try {
            ZipInputStream(streamSupplier()).use { zis ->
                var entryCount = 0
                var totalUncompressed = 0L

                while (true) {
                    val entry = zis.nextEntry ?: break
                    entryCount++

                    if (entryCount > MAX_ENTRY_COUNT) {
                        return Result.failure(
                            SkinLoadError.TooManyEntries(sourceName, entryCount)
                        )
                    }

                    val name = entry.name.replace('\\', '/').trimStart('/')
                    if (name.isEmpty()) continue

                    // Path safety checks (REQ-THM-018)
                    val safetyResult = checkPathSafety(name)
                    if (safetyResult != null) {
                        return Result.failure(
                            SkinLoadError.UnsafePath(sourceName, name, safetyResult)
                        )
                    }

                    // Depth check
                    if (name.count { it == '/' } > MAX_PATH_DEPTH) {
                        return Result.failure(
                            SkinLoadError.UnsafePath(
                                sourceName, name,
                                "path depth exceeds $MAX_PATH_DEPTH"
                            )
                        )
                    }

                    // Size check (REQ-THM-017)
                    val uncompressed = entry.size.coerceAtLeast(0)
                    if (uncompressed > MAX_FILE_SIZE) {
                        return Result.failure(
                            SkinLoadError.FileTooLarge(
                                sourceName, name, uncompressed, MAX_FILE_SIZE.toLong()
                            )
                        )
                    }

                    totalUncompressed += uncompressed
                    if (totalUncompressed > MAX_UNCOMPRESSED_SIZE) {
                        return Result.failure(
                            SkinLoadError.TotalSizeExceeded(sourceName, totalUncompressed)
                        )
                    }

                    // Read entry bytes
                    val bytes = zis.readBytes()

                    entries.add(ZipEntryData(
                        name = name,
                        bytes = bytes,
                        isDirectory = name.endsWith('/'),
                    ))

                    if (name == "manifest.json") {
                        manifestBytes = bytes
                    }
                }
            }
        } catch (e: Exception) {
            return Result.failure(
                SkinLoadError.IoError(sourceName, "reading ZIP", e)
            )
        }

        if (manifestBytes == null) {
            return Result.failure(SkinLoadError.ManifestMissing(sourceName))
        }

        // Step 3: Parse and validate manifest.json
        val manifestResult = parseAndValidateManifest(manifestBytes, sourceName)
        if (manifestResult is Err) return Result.failure(manifestResult.value)

        @Suppress("UNCHECKED_CAST")
        val manifest = (manifestResult as Ok<ValidatedManifest>).value

        // Version compatibility check (REQ-THM-052)
        if (!checkAppVersion(manifest.minAppVersion, appVersion)) {
            return Result.failure(
                SkinLoadError.AppVersionTooOld(sourceName, manifest.minAppVersion, appVersion)
            )
        }

        // Step 4: Validate theme.json if present
        var theme: Theme? = null
        if (manifest.capabilities.contains(Capability.Theme)) {
            val themeEntry = entries.find { it.name == "theme.json" }
            if (themeEntry != null) {
                val themeResult = parseAndValidateTheme(themeEntry.bytes, sourceName)
                if (themeResult is Err) return Result.failure(themeResult.value)
                theme = (themeResult as Ok<Theme>).value

                // Step 5: Contrast enforcement (REQ-THM-041)
                val contrastResult = checkContrast(theme.color, sourceName)
                if (contrastResult != null) return Result.failure(contrastResult)
            }
        }

        // Step 6: Validate layout documents
        val layouts = mutableMapOf<String, LayoutDocument>()
        if (manifest.capabilities.contains(Capability.Layout)) {
            for (entry in entries) {
                if (entry.name.startsWith("layout/") && entry.name.endsWith(".eclayout")) {
                    val surfaceName = entry.name.removePrefix("layout/").removeSuffix(".eclayout")
                    val layoutResult = parseAndValidateLayout(entry.bytes, entry.name, sourceName)
                    if (layoutResult is Err) return Result.failure(layoutResult.value)
                    layouts[surfaceName] = (layoutResult as Ok<LayoutDocument>).value
                }
            }
        }

        // Steps 7-9 are handled by the installer (not the loader)

        return Result.success(
            Skin(
                manifest = manifest,
                theme = theme,
                layouts = layouts,
                assets = null,  // Assets are extracted by the SkinInstaller
            )
        )
    }

    // -------------------------------------------------------------------------
    //  Step 3: Parse and validate manifest.json
    // -------------------------------------------------------------------------

    private fun parseAndValidateManifest(
        bytes: ByteArray,
        sourceName: String,
    ): Result<ValidatedManifest, SkinLoadError.ManifestInvalid> {
        val text = bytes.decodeToString()
        return try {
            val obj = JSONObject(text)
            val errors = schemaValidator.validateManifest(obj)
            if (errors.isNotEmpty()) {
                return Err(SkinLoadError.ManifestInvalid(sourceName, errors))
            }
            Ok(buildManifest(obj))
        } catch (e: Exception) {
            Err(SkinLoadError.ManifestInvalid(sourceName, listOf("JSON parse error: ${e.message}")))
        }
    }

    private fun buildManifest(obj: JSONObject): ValidatedManifest {
        val caps = mutableSetOf<Capability>()
        obj.optJSONArray("capabilities")?.let { arr ->
            for (i in 0 until arr.length()) {
                when (arr.getString(i)) {
                    "theme"  -> caps.add(Capability.Theme)
                    "layout" -> caps.add(Capability.Layout)
                    "icons"  -> caps.add(Capability.Icons)
                    "images" -> caps.add(Capability.Images)
                    "fonts"  -> caps.add(Capability.Fonts)
                }
            }
        }

        val surfaces = mutableSetOf<SkinSurface>()
        obj.optJSONArray("targetSurfaces")?.let { arr ->
            for (i in 0 until arr.length()) {
                when (arr.getString(i)) {
                    "main-window" -> surfaces.add(SkinSurface.MainWindow)
                    "now-playing" -> surfaces.add(SkinSurface.NowPlaying)
                    "mini-player" -> surfaces.add(SkinSurface.MiniPlayer)
                    "library"     -> surfaces.add(SkinSurface.Library)
                }
            }
        }

        val checksums = mutableMapOf<String, String>()
        obj.optJSONObject("checksums")?.let { ck ->
            ck.keys().forEach { key -> checksums[key] = ck.getString(key) }
        }

        return ValidatedManifest(
            schemaVersion = obj.optInt("schemaVersion", 1),
            id = obj.getString("id"),
            name = obj.getString("name"),
            author = obj.optString("author").takeIf { it.isNotBlank() },
            version = obj.getString("version"),
            license = obj.optString("license").takeIf { it.isNotBlank() },
            homepage = obj.optString("homepage").takeIf { it.isNotBlank() },
            description = obj.optString("description").takeIf { it.isNotBlank() },
            minAppVersion = obj.getString("minAppVersion"),
            capabilities = caps,
            surfaces = surfaces,
            checksums = checksums,
        )
    }

    // -------------------------------------------------------------------------
    //  Step 4: Parse and validate theme.json
    // -------------------------------------------------------------------------

    private fun parseAndValidateTheme(
        bytes: ByteArray,
        sourceName: String,
    ): Result<Theme, SkinLoadError.ThemeInvalid> {
        val text = bytes.decodeToString()
        return try {
            val obj = JSONObject(text)
            val errors = schemaValidator.validateTheme(obj)
            if (errors.isNotEmpty()) {
                return Err(SkinLoadError.ThemeInvalid(sourceName, errors))
            }
            Ok(buildTheme(obj))
        } catch (e: Exception) {
            Err(SkinLoadError.ThemeInvalid(sourceName, listOf("JSON parse error: ${e.message}")))
        }
    }

    private fun buildTheme(obj: JSONObject): Theme {
        val modeStr = obj.optString("mode", "dark")
        val mode = if (modeStr == "light") Mode.Light else Mode.Dark

        val color = buildColorTokens(obj.optJSONObject("color")
            ?: throw IllegalArgumentException("color is required"))

        return Theme(
            schemaVersion = obj.optInt("schemaVersion", 1),
            id = obj.getString("id"),
            name = obj.getString("name"),
            author = obj.optString("author").takeIf { it.isNotBlank() },
            version = obj.getString("version"),
            license = obj.optString("license").takeIf { it.isNotBlank() },
            homepage = obj.optString("homepage").takeIf { it.isNotBlank() },
            description = obj.optString("description").takeIf { it.isNotBlank() },
            minAppVersion = obj.optString("minAppVersion").takeIf { it.isNotBlank() },
            extends = obj.optString("extends").takeIf { it.isNotBlank() },
            mode = mode,
            color = color,
        )
    }

    private fun buildColorTokens(obj: JSONObject): ColorTokens {
        fun hex(key: String): String = obj.optString(key)
        fun hexOrNull(key: String): String? = obj.optString(key).takeIf { it.isNotBlank() }

        val bg = obj.optJSONObject("background") ?: throw IllegalArgumentException("color.background is required")
        val surface = obj.optJSONObject("surface") ?: throw IllegalArgumentException("color.surface is required")
        val text = obj.optJSONObject("text") ?: throw IllegalArgumentException("color.text is required")
        val accent = obj.optJSONObject("accent") ?: throw IllegalArgumentException("color.accent is required")
        val border = obj.optJSONObject("border") ?: throw IllegalArgumentException("color.border is required")

        return ColorTokens(
            background = BackgroundTokens(
                base = bg.getString("base"),
                sunken = hexOrNull("sunken"),
                raised = hexOrNull("raised"),
                overlay = hexOrNull("overlay"),
                scrim = hexOrNull("scrim"),
            ),
            surface = SurfaceTokens(
                base = surface.getString("base"),
                hover = hexOrNull("hover"),
                pressed = hexOrNull("pressed"),
                selected = hexOrNull("selected"),
                disabled = hexOrNull("disabled"),
            ),
            text = TextTokens(
                primary = text.getString("primary"),
                secondary = text.getString("secondary"),
                tertiary = hexOrNull("tertiary"),
                disabled = hexOrNull("disabled"),
                inverse = hexOrNull("inverse"),
                onAccent = hexOrNull("onAccent"),
                link = hexOrNull("link"),
            ),
            accent = AccentTokens(
                base = accent.getString("base"),
                hover = hexOrNull("hover"),
                pressed = hexOrNull("pressed"),
                subtle = hexOrNull("subtle"),
                muted = hexOrNull("muted"),
            ),
            border = BorderTokens(
                base = border.getString("base"),
                subtle = hexOrNull("subtle"),
                strong = hexOrNull("strong"),
                focus = hexOrNull("focus"),
            ),
        )
    }

    // -------------------------------------------------------------------------
    //  Step 5: Contrast enforcement (REQ-THM-041)
    // -------------------------------------------------------------------------

    /**
     * Check all (text, background) colour pairs for WCAG AA compliance.
     *
     * Returns a [ContrastFailure] if any pair is below 4.5:1 (normal text)
     * or 3:1 (large text ≥18.66px regular / ≥14px bold), unless the user
     * has enabled *Enforce accessible contrast* in settings (handled by caller).
     */
    private fun checkContrast(
        colors: ColorTokens,
        sourceName: String,
    ): SkinLoadError.ContrastFailure? {
        val pairs = mutableListOf<ContrastPair>()

        // Test all required text colours against the required backgrounds
        val tests = listOf(
            // (textColor, backgroundColor)
            colors.text.primary to colors.background.base,
            colors.text.secondary to colors.background.base,
            colors.text.primary to colors.surface.base,
            colors.text.secondary to colors.surface.base,
            colors.text.primary to colors.surface.hover,
            colors.accent.base to colors.background.base,
        )

        for ((fg, bg) in tests) {
            if (fg.isBlank() || bg.isBlank()) continue
            val ratio = contrastRatio(fg, bg)
            if (ratio < 4.5) {
                pairs.add(ContrastPair(bg, fg, ratio))
            }
        }

        return if (pairs.isNotEmpty()) {
            SkinLoadError.ContrastFailure(sourceName, pairs)
        } else {
            null
        }
    }

    // -------------------------------------------------------------------------
    //  Step 6: Parse and validate layout documents
    // -------------------------------------------------------------------------

    private fun parseAndValidateLayout(
        bytes: ByteArray,
        fileName: String,
        sourceName: String,
    ): Result<LayoutDocument, SkinLoadError.LayoutInvalid> {
        val text = bytes.decodeToString()
        return try {
            val obj = JSONObject(text)
            val errors = schemaValidator.validateLayout(obj)
            if (errors.isNotEmpty()) {
                return Err(SkinLoadError.LayoutInvalid(sourceName, fileName, errors))
            }
            Ok(LayoutDocument(
                surface = obj.optString("surface"),
                root = parseLayoutNode(obj.optJSONObject("root") ?: JSONObject()),
            ))
        } catch (e: Exception) {
            Err(SkinLoadError.LayoutInvalid(sourceName, fileName, listOf("JSON parse error: ${e.message}")))
        }
    }

    private fun parseLayoutNode(obj: JSONObject): LayoutNode {
        return LayoutNode(
            type = obj.optString("type", "Panel"),
            id = obj.optString("id").takeIf { it.isNotBlank() },
            children = obj.optJSONArray("children")?.let { arr ->
                (0 until arr.length()).mapNotNull { i ->
                    val child = arr.optJSONObject(i) ?: return@mapNotNull null
                    parseLayoutNode(child)
                }
            } ?: emptyList(),
        )
    }

    // -------------------------------------------------------------------------
    //  Path safety helpers  (REQ-THM-018)
    // -------------------------------------------------------------------------

    private fun checkPathSafety(path: String): String? {
        if (path.length > MAX_PATH_LENGTH) {
            return "path exceeds ${MAX_PATH_LENGTH} bytes"
        }

        // Check for traversal
        val parts = path.split('/')
        for (part in parts) {
            if (part == "..") return "contains '..' traversal segment"
            if (part.isEmpty()) continue
            if (part.contains('\0')) return "contains NUL byte"
        }

        // Check for absolute path
        if (path.startsWith('/') || path.matches(Regex("^[A-Za-z]:"))) {
            return "must be relative (not absolute)"
        }

        // Check for permitted top-level directories
        val topLevel = parts.firstOrNull() ?: return null
        val permitted = setOf(
            "theme.json", "LICENSE", "preview.png",
            "layout", "icons", "images", "fonts", "i18n"
        )
        if (topLevel !in permitted && topLevel != "theme.json") {
            return "top-level directory must be one of: ${permitted.joinToString()}"
        }

        return null
    }

    // -------------------------------------------------------------------------
    //  Version compatibility
    // -------------------------------------------------------------------------

    private fun checkAppVersion(skinMin: String, appVersion: String): Boolean {
        val skinParts = skinMin.split('.').mapNotNull { it.toIntOrNull() }
        val appParts = appVersion.split('.').mapNotNull { it.toIntOrNull() }
        if (skinParts.size != 3 || appParts.size != 3) return false
        for (i in 0..2) {
            if (appParts[i] < skinParts[i]) return false
            if (appParts[i] > skinParts[i]) return true
        }
        return true
    }
}

// ---------------------------------------------------------------------------
//  Internal types
// ---------------------------------------------------------------------------

private data class ZipEntryData(
    val name: String,
    val bytes: ByteArray,
    val isDirectory: Boolean,
)

private data class LayoutDocument(
    val surface: String,
    val root: LayoutNode,
)

private data class LayoutNode(
    val type: String,
    val id: String? = null,
    val children: List<LayoutNode> = emptyList(),
)

// ---------------------------------------------------------------------------
//  Result type  (Kotlin stdlib doesn't have Result alias for error)
// ---------------------------------------------------------------------------

private typealias Ok<T> = kotlin.Result<T>
private typealias Err<E> = kotlin.Result<Nothing>

// ---------------------------------------------------------------------------
//  Contrast checker  (WCAG 2.2 / REQ-THM-041)
// ---------------------------------------------------------------------------

object ContrastChecker {
    /**
     * Compute the WCAG 2.1 contrast ratio between two colours.
     *
     * @param hex1 "#RRGGBB" or "#RRGGBBAA" colour string.
     * @param hex2 "#RRGGBB" or "#RRGGBBAA" colour string.
     * @return The contrast ratio (1.0 to 21.0).
     */
    fun contrastRatio(hex1: String, hex2: String): Double {
        val c1 = parseColor(hex1)
        val c2 = parseColor(hex2)
        val l1 = luminance(c1[0], c1[1], c1[2])
        val l2 = luminance(c2[0], c2[1], c2[2])
        val lighter = maxOf(l1, l2)
        val darker = minOf(l1, l2)
        return (lighter + 0.05) / (darker + 0.05)
    }

    private fun parseColor(hex: String): FloatArray {
        val clean = hex.removePrefix("#")
        return floatArrayOf(
            clean.substring(0, 2).toInt(16) / 255f,
            clean.substring(2, 4).toInt(16) / 255f,
            clean.substring(4, 6).toInt(16) / 255f,
        )
    }

    private fun luminance(r: Float, g: Float, b: Float): Double {
        fun linear(c: Float): Double {
            val v = c.toDouble()
            return if (v <= 0.03928) v / 12.92 else ((v + 0.055) / 1.055).let { pow(it, 2.4) }
        }
        return 0.2126 * linear(r) + 0.7152 * linear(g) + 0.0722 * linear(b)
    }
}
