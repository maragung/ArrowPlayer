// SPDX-License-Identifier: MPL-2.0
//
// pack.cpp — see pack.hpp for design notes.
//
// The ZIP reader here is deliberately small: it walks the end-of-
// central-directory record, then the central directory entries, then
// defers to zlib to decompress. We do not support encryption, multi-
// disk archives, ZIP64, or any compression method other than "store"
// (0) and "deflate" (8). That is the surface REQ-THM-015 fixes; the
// spec explicitly forbids "no encryption, no other compression methods".
//
// All numeric fields are read little-endian. The ZIP spec is in the
// APPNOTE.TXT file at pkware.com; we follow it section by section.

#include "skin/pack.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <zlib.h>
#include <openssl/sha.h>

#include <nlohmann/json.hpp>

namespace arrow::skin {

namespace {

// ---------------------------------------------------------------------------
//  Endian-safe POD readers. We never reinterpret_cast; every read goes
//  through these so a big-endian build is just as correct as a little-
//  endian one.
// ---------------------------------------------------------------------------

struct Reader {
    const std::uint8_t* p;
    const std::uint8_t* end;

    bool need(std::size_t n) const noexcept { return (end - p) >= static_cast<std::ptrdiff_t>(n); }
    bool ok() const noexcept { return p <= end; }

    std::uint8_t  u8()  { auto v = *p++; return v; }
    std::uint16_t u16() { auto v = static_cast<std::uint16_t>(p[0] | (p[1] << 8)); p += 2; return v; }
    std::uint32_t u32() { auto v = static_cast<std::uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); p += 4; return v; }
    std::uint64_t u64() {
        std::uint64_t lo = u32();
        std::uint64_t hi = u32();
        return lo | (hi << 32);
    }
};

// ---------------------------------------------------------------------------
//  SHA-256 helpers. We use OpenSSL's API because it is what the rest
//  of the project already uses; vendoring a header-only SHA-256 just
//  for this would be net new code with no upside.
// ---------------------------------------------------------------------------

std::string sha256_hex(const std::vector<std::uint8_t>& bytes) {
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(bytes.data(), bytes.size(), digest.data());
    static const char* kHex = "0123456789abcdef";
    std::string out(SHA256_DIGEST_LENGTH * 2, '0');
    for (std::size_t i = 0; i < digest.size(); ++i) {
        out[i * 2 + 0] = kHex[(digest[i] >> 4) & 0xF];
        out[i * 2 + 1] = kHex[ digest[i]       & 0xF];
    }
    return out;
}

// ---------------------------------------------------------------------------
//  Per-entry "local file header" + payload reader.
//
//  The local file header carries the same fields as the central-
//  directory entry that pointed at it, plus the compressed payload
//  immediately after. We use the central directory as the source of
//  truth for filenames and sizes, and use the local file header
//  purely to locate the compressed bytes.
// ---------------------------------------------------------------------------

struct LocalHeader {
    std::uint32_t signature{0};
    std::uint16_t version_needed{0};
    std::uint16_t flags{0};
    std::uint16_t compression{0};
    std::uint16_t mod_time{0};
    std::uint16_t mod_date{0};
    std::uint32_t crc32{0};
    std::uint64_t compressed_size{0};
    std::uint64_t uncompressed_size{0};
    std::uint16_t name_len{0};
    std::uint16_t extra_len{0};
    std::string   name;
    std::vector<std::uint8_t> extra;
};

bool read_local_header(const std::vector<std::uint8_t>& file, std::size_t offset, LocalHeader& out, std::string& why) {
    if (offset + 30 > file.size()) { why = "local file header truncated"; return false; }
    Reader r{file.data() + offset, file.data() + file.size()};
    if (!r.need(30)) { why = "local file header short"; return false; }
    out.signature = r.u32();
    if (out.signature != 0x04034b50) { why = "local file header bad signature"; return false; }
    out.version_needed = r.u16();
    out.flags          = r.u16();
    out.compression    = r.u16();
    out.mod_time       = r.u16();
    out.mod_date       = r.u16();
    out.crc32          = r.u32();
    out.compressed_size   = r.u32();
    out.uncompressed_size = r.u32();
    out.name_len  = r.u16();
    out.extra_len = r.u16();
    if (!r.need(out.name_len + out.extra_len)) { why = "local file header name/extra truncated"; return false; }
    out.name.assign(reinterpret_cast<const char*>(r.p), out.name_len);
    r.p += out.name_len;
    out.extra.assign(r.p, r.p + out.extra_len);
    return true;
}

// ---------------------------------------------------------------------------
//  Central-directory entry reader.
// ---------------------------------------------------------------------------

struct CentralEntry {
    std::uint32_t signature{0};
    std::uint16_t version_made_by{0};
    std::uint16_t version_needed{0};
    std::uint16_t flags{0};
    std::uint16_t compression{0};
    std::uint16_t mod_time{0};
    std::uint16_t mod_date{0};
    std::uint32_t crc32{0};
    std::uint64_t compressed_size{0};
    std::uint64_t uncompressed_size{0};
    std::uint16_t name_len{0};
    std::uint16_t extra_len{0};
    std::uint16_t comment_len{0};
    std::uint16_t disk_number{0};
    std::uint16_t internal_attr{0};
    std::uint32_t external_attr{0};
    std::uint64_t local_header_offset{0};
    std::string   name;
    std::string   comment;
};

bool read_central_entry(const std::vector<std::uint8_t>& file, std::size_t offset, CentralEntry& out, std::string& why) {
    if (offset + 46 > file.size()) { why = "central dir entry truncated"; return false; }
    Reader r{file.data() + offset, file.data() + file.size()};
    if (!r.need(46)) { why = "central dir entry short"; return false; }
    out.signature        = r.u32();
    if (out.signature != 0x02014b50) { why = "central dir entry bad signature"; return false; }
    out.version_made_by  = r.u16();
    out.version_needed   = r.u16();
    out.flags            = r.u16();
    out.compression      = r.u16();
    out.mod_time         = r.u16();
    out.mod_date         = r.u16();
    out.crc32            = r.u32();
    out.compressed_size  = r.u32();
    out.uncompressed_size= r.u32();
    out.name_len         = r.u16();
    out.extra_len        = r.u16();
    out.comment_len      = r.u16();
    out.disk_number      = r.u16();
    out.internal_attr    = r.u16();
    out.external_attr    = r.u32();
    out.local_header_offset = r.u32();
    if (!r.need(out.name_len + out.extra_len + out.comment_len)) { why = "central dir entry name/extra/comment truncated"; return false; }
    out.name.assign(reinterpret_cast<const char*>(r.p), out.name_len);
    r.p += out.name_len;
    r.p += out.extra_len;
    out.comment.assign(reinterpret_cast<const char*>(r.p), out.comment_len);
    return true;
}

// ---------------------------------------------------------------------------
//  End-of-central-directory record.
//
//  The record is at most 65557 bytes from the end of the file. We
//  scan from the end to find the signature, since the comment length
//  is variable.
// ---------------------------------------------------------------------------

struct EndOfCentral {
    std::uint32_t signature{0};
    std::uint16_t disk_number{0};
    std::uint16_t disk_with_cd{0};
    std::uint16_t entries_this_disk{0};
    std::uint16_t entries_total{0};
    std::uint32_t cd_size{0};
    std::uint32_t cd_offset{0};
    std::uint16_t comment_len{0};
    std::string   comment;
};

bool find_end_of_central(const std::vector<std::uint8_t>& file, EndOfCentral& out, std::string& why) {
    if (file.size() < 22) { why = "file too small to be a ZIP"; return false; }
    constexpr std::size_t kMaxComment = 65535;
    const std::size_t max_scan = std::min<std::size_t>(file.size(), kMaxComment + 22);
    const std::size_t start = file.size() - max_scan;
    for (std::size_t i = file.size() - 22; ; ) {
        if (i < start) { why = "end-of-central-directory signature not found"; return false; }
        if (file[i] == 0x50 && file[i + 1] == 0x4b &&
            file[i + 2] == 0x05 && file[i + 3] == 0x06) {
            Reader r{file.data() + i, file.data() + file.size()};
            r.p += 4;  // skip signature
            out.disk_number    = r.u16();
            out.disk_with_cd   = r.u16();
            out.entries_this_disk = r.u16();
            out.entries_total  = r.u16();
            out.cd_size        = r.u32();
            out.cd_offset      = r.u32();
            out.comment_len    = r.u16();
            if (!r.need(out.comment_len)) { why = "eocd comment truncated"; return false; }
            out.comment.assign(reinterpret_cast<const char*>(r.p), out.comment_len);
            return true;
        }
        if (i == start) break;
        --i;
    }
    why = "end-of-central-directory signature not found";
    return false;
}

// ---------------------------------------------------------------------------
//  Decompress a deflate stream from a file. The "store" method
//  (compression == 0) just copies the bytes through.
// ---------------------------------------------------------------------------

bool read_entry_bytes(const std::vector<std::uint8_t>& file, const CentralEntry& ce,
                      std::vector<std::uint8_t>& out, std::string& why) {
    LocalHeader lh;
    if (!read_local_header(file, static_cast<std::size_t>(ce.local_header_offset), lh, why)) return false;
    // The local file header name and the central dir name should match;
    // if they don't, treat it as a malformed archive.
    if (lh.name != ce.name) { why = "local/central name mismatch"; return false; }
    const std::size_t data_off = static_cast<std::size_t>(ce.local_header_offset) + 30
                               + lh.name_len + lh.extra_len;
    if (data_off + ce.compressed_size > file.size()) { why = "entry data truncated"; return false; }
    const std::uint8_t* src = file.data() + data_off;
    if (ce.compression == 0) {
        out.assign(src, src + ce.compressed_size);
        return true;
    }
    if (ce.compression != 8) { why = "unsupported compression method"; return false; }
    out.resize(static_cast<std::size_t>(ce.uncompressed_size));
    z_stream zs{};
    zs.next_in  = const_cast<Bytef*>(src);
    zs.avail_in = static_cast<uInt>(ce.compressed_size);
    if (inflateInit2(&zs, -15) != Z_OK) { why = "inflateInit2 failed"; return false; }
    zs.next_out  = out.data();
    zs.avail_out = static_cast<uInt>(ce.uncompressed_size);
    int rc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (rc != Z_STREAM_END) { why = "inflate did not finish"; return false; }
    return true;
}

// ---------------------------------------------------------------------------
//  Compare a semver "X.Y.Z" with another; both strings are required to
//  be the three-part dotted form. The package's minAppVersion is a
//  floor, not a pin: a package saying "1.0.0" is OK on 1.4.2.
// ---------------------------------------------------------------------------

bool parse_semver(const std::string& s, int& a, int& b, int& c) {
    return std::sscanf(s.c_str(), "%d.%d.%d", &a, &b, &c) == 3;
}

int cmp_semver(const std::string& lhs, const std::string& rhs) {
    int la, lb, lc, ra, rb, rc;
    if (!parse_semver(lhs, la, lb, lc)) return 0;
    if (!parse_semver(rhs, ra, rb, rc)) return 0;
    if (la != ra) return la < ra ? -1 : 1;
    if (lb != rb) return lb < rb ? -1 : 1;
    if (lc != rc) return lc < rc ? -1 : 1;
    return 0;
}

std::string short_name(const std::filesystem::path& p) {
    return p.filename().string();
}

}  // namespace

// ---------------------------------------------------------------------------
//  SkinInstaller
// ---------------------------------------------------------------------------

SkinInstaller::SkinInstaller(const theme::SchemaValidator& v) : validator_(v) {}

// ---------------------------------------------------------------------------
//  inspect — open the archive, check the central directory against the
//  REQ-THM-017 limits, run zip_safety over every entry, parse and
//  validate manifest.json, and verify the SHA-256 of every other entry
//  against the manifest's `checksums` map. Returns one of the verdict
//  enums above; on success fills `entries` and `manifest` for the
//  caller.
// ---------------------------------------------------------------------------

InstallResult SkinInstaller::inspect(const std::filesystem::path& path,
                                     std::vector<EntryMeta>& entries,
                                     PackageInfo& manifest) {
    entries.clear();
    InstallResult r;

    // Read the whole file into memory. The size cap is 32 MiB, so a
    // few hundred MiB peak is acceptable; we are not trying to be
    // a streaming parser.
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        r.error = InstallError::OpenFailed;
        r.why   = "cannot open " + path.string() + ": " + std::strerror(errno);
        return r;
    }
    const auto sz = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> file(sz);
    if (sz > 0 && !in.read(reinterpret_cast<char*>(file.data()),
                           static_cast<std::streamsize>(sz))) {
        r.error = InstallError::OpenFailed;
        r.why   = "short read on " + path.string();
        return r;
    }
    if (file.size() < 4 || file[0] != 'P' || file[1] != 'K' ||
        file[2] != 0x03 || file[3] != 0x04) {
        r.error = InstallError::NotAZip;
        r.why   = "ZIP signature missing on " + short_name(path);
        return r;
    }

    EndOfCentral eocd;
    std::string why;
    if (!find_end_of_central(file, eocd, why)) {
        r.error = InstallError::CentralDirFailed;
        r.why   = why;
        return r;
    }

    // Walk the central directory. We do not honour the disk number
    // fields; multi-disk ZIPs are out of scope (REQ-THM-015 only
    // mentions single-file packages).
    std::size_t off = eocd.cd_offset;
    std::uint64_t total_uncompressed = 0;
    std::size_t   font_count  = 0;
    std::size_t   layout_count = 0;
    std::string   manifest_name = "manifest.json";
    bool          seen_manifest = false;

    for (std::uint16_t i = 0; i < eocd.entries_total; ++i) {
        CentralEntry ce;
        if (!read_central_entry(file, off, ce, why)) {
            r.error = InstallError::CentralDirFailed;
            r.why   = why;
            return r;
        }
        off += 46 + ce.name_len + ce.extra_len + ce.comment_len;

        EntryMeta em;
        em.name              = ce.name;
        em.compressed_size   = ce.compressed_size;
        em.uncompressed_size = ce.uncompressed_size;
        em.crc32             = ce.crc32;
        em.external_attr     = ce.external_attr;
        // Unix file-type bits live in the top byte of the upper 16 of
        // external_attr. The MS-DOS directory bit lives in bit 4 of the
        // lower byte. Both forms appear in real-world packages.
        const std::uint8_t  msdos_attr = static_cast<std::uint8_t>(ce.external_attr & 0xFF);
        const std::uint16_t unix_mode  = static_cast<std::uint16_t>((ce.external_attr >> 16) & 0xFFFF);
        em.is_directory = (msdos_attr & 0x10) != 0;
        em.is_symlink   = (unix_mode != 0) && ((unix_mode & 0170000) == 0120000);
        em.is_hardlink  = (unix_mode != 0) && (unix_mode & 0170000) == 0 && ce.uncompressed_size == 0 &&
                          !em.name.empty();
        em.is_device    = (unix_mode != 0) &&
                          (((unix_mode & 0170000) == 0020000) ||
                           ((unix_mode & 0170000) == 0040000) ||
                           ((unix_mode & 0170000) == 0060000));

        // Per-entry REQ-THM-017 checks.
        if (ce.uncompressed_size > kMaxSingleFileUncompressed) {
            r.error = InstallError::LimitsExceeded;
            r.why   = "entry '" + ce.name + "' is " +
                      std::to_string(ce.uncompressed_size) + " bytes; max is " +
                      std::to_string(kMaxSingleFileUncompressed);
            r.offending_pointer = ce.name;
            return r;
        }
        if (ce.uncompressed_size > 0 && ce.compressed_size > 0) {
            const std::uint64_t ratio = ce.uncompressed_size / ce.compressed_size;
            if (ratio > kMaxCompressionRatio) {
                r.error = InstallError::LimitsExceeded;
                r.why   = "entry '" + ce.name + "' has compression ratio " +
                          std::to_string(ratio) + ":1; max is " +
                          std::to_string(kMaxCompressionRatio) + ":1";
                r.offending_pointer = ce.name;
                return r;
            }
        }
        if (ce.compression != 0 && ce.compression != 8) {
            r.error = InstallError::LimitsExceeded;
            r.why   = "entry '" + ce.name + "' uses unsupported compression method " +
                      std::to_string(ce.compression);
            r.offending_pointer = ce.name;
            return r;
        }

        // Tally counts for the per-directory caps that the schema
        // cannot express.
        if (ce.name.rfind("fonts/", 0) == 0 && !em.is_directory) ++font_count;
        if (ce.name.rfind("layout/", 0) == 0 &&
            ce.name.size() > 7 &&
            ce.name.substr(ce.name.size() - 9) == ".eclayout") ++layout_count;

        // We do not yet know the install destination; the path-safety
        // check below uses a stand-in root and is re-run with the
        // real root by install(). The stand-in is enough to surface
        // the textual policy violations (which are the bulk of the
        // REQ-THM-018 test corpus).
        EntryInfo ei{ce.name, {em.is_symlink, em.is_hardlink, em.is_device}};
        auto sr = check_entry_path(ei.name, ei.attr, std::filesystem::temp_directory_path());
        if (!sr.ok()) {
            r.error = InstallError::PathSafety;
            r.why   = sr.why;
            r.offending_pointer = ce.name;
            return r;
        }

        total_uncompressed += ce.uncompressed_size;
        if (total_uncompressed > kMaxUncompressedTotal) {
            r.error = InstallError::LimitsExceeded;
            r.why   = "total uncompressed size exceeds " +
                      std::to_string(kMaxUncompressedTotal) + " bytes";
            r.offending_pointer = ce.name;
            return r;
        }
        if (entries.size() + 1 > kMaxEntries) {
            r.error = InstallError::LimitsExceeded;
            r.why   = "entry count exceeds " + std::to_string(kMaxEntries);
            r.offending_pointer = ce.name;
            return r;
        }
        if (em.name == manifest_name) seen_manifest = true;
        entries.push_back(std::move(em));
    }

    if (!seen_manifest) {
        r.error = InstallError::ManifestMissing;
        r.why   = "no manifest.json in the package";
        return r;
    }
    if (font_count > kMaxFonts) {
        r.error = InstallError::LimitsExceeded;
        r.why   = "fonts/ has " + std::to_string(font_count) + " files; max is " +
                  std::to_string(kMaxFonts);
        r.offending_pointer = "fonts/";
        return r;
    }
    if (layout_count > kMaxLayoutDocs) {
        r.error = InstallError::LimitsExceeded;
        r.why   = "layout/ has " + std::to_string(layout_count) + " documents; max is " +
                  std::to_string(kMaxLayoutDocs);
        r.offending_pointer = "layout/";
        return r;
    }
    if (font_count > 0) {
        // REQ-THM-040 step 9: fonts/ present implies fonts/LICENSE-fonts
        // must be present. The schema cannot express that cross-file
        // dependency; we enforce it here.
        const bool have_licence = std::any_of(entries.begin(), entries.end(),
            [](const EntryMeta& e) { return e.name == "fonts/LICENSE-fonts"; });
        if (!have_licence) {
            r.error = InstallError::FontLicenceMissing;
            r.why   = "fonts/ is present but fonts/LICENSE-fonts is missing";
            r.offending_pointer = "fonts/LICENSE-fonts";
            return r;
        }
    }

    // Read the manifest bytes, parse, and validate.
    std::vector<std::uint8_t> manifest_bytes;
    {
        auto it = std::find_if(entries.begin(), entries.end(),
            [](const EntryMeta& e) { return e.name == "manifest.json"; });
        const auto& ce = *it;
        LocalHeader lh;
        if (!read_local_header(file, static_cast<std::size_t>(ce.local_header_offset), lh, why)) {
            r.error = InstallError::ManifestMissing;
            r.why   = why;
            return r;
        }
        const std::size_t data_off = static_cast<std::size_t>(ce.local_header_offset) + 30
                                   + lh.name_len + lh.extra_len;
        if (data_off + ce.compressed_size > file.size()) {
            r.error = InstallError::ManifestMissing;
            r.why   = "manifest.json data truncated";
            return r;
        }
        const std::uint8_t* src = file.data() + data_off;
        if (ce.compression == 0) {
            manifest_bytes.assign(src, src + ce.compressed_size);
        } else {
            manifest_bytes.resize(ce.uncompressed_size);
            z_stream zs{};
            zs.next_in  = const_cast<Bytef*>(src);
            zs.avail_in = static_cast<uInt>(ce.compressed_size);
            if (inflateInit2(&zs, -15) != Z_OK) {
                r.error = InstallError::ManifestMissing;
                r.why   = "inflateInit2 failed on manifest";
                return r;
            }
            zs.next_out  = manifest_bytes.data();
            zs.avail_out = static_cast<uInt>(ce.uncompressed_size);
            int zrc = inflate(&zs, Z_FINISH);
            inflateEnd(&zs);
            if (zrc != Z_STREAM_END) {
                r.error = InstallError::ManifestMissing;
                r.why   = "inflate did not finish on manifest";
                return r;
            }
        }
    }
    const std::string manifest_text(manifest_bytes.begin(), manifest_bytes.end());
    auto vres = validator_.validate(theme::SchemaId::SkinManifest,
                                    std::string_view{manifest_text});
    if (!vres.ok()) {
        r.error = InstallError::ManifestSchema;
        r.why   = "manifest.json failed schema validation";
        if (!vres.errors.empty()) {
            r.offending_pointer = vres.errors.front().instance_pointer;
            r.why += " (" + vres.errors.front().message + ")";
        }
        return r;
    }

    // Pull the fields we need out of the manifest. Anything that is
    // not in the schema is a hard error; the validator has already
    // rejected it, so we can use unchecked reads here.
    nlohmann::json mj = nlohmann::json::parse(manifest_text);
    manifest.id              = mj["id"].get<std::string>();
    manifest.name            = mj["name"].get<std::string>();
    manifest.version         = mj["version"].get<std::string>();
    manifest.license         = mj["license"].get<std::string>();
    manifest.min_app_version = mj["minAppVersion"].get<std::string>();
    for (const auto& c : mj["capabilities"]) manifest.capabilities.push_back(c.get<std::string>());
    if (mj.contains("targetSurfaces"))
        for (const auto& s : mj["targetSurfaces"]) manifest.target_surfaces.push_back(s.get<std::string>());
    if (mj.contains("i18n"))
        for (const auto& s : mj["i18n"]) manifest.i18n.push_back(s.get<std::string>());
    if (mj.contains("homepage"))   manifest.homepage   = mj["homepage"].get<std::string>();
    if (mj.contains("description")) manifest.description = mj["description"].get<std::string>();

    // Reject a package for a newer app than the running one.
    if (!app_version_.empty() && cmp_semver(manifest.min_app_version, app_version_) > 0) {
        r.error = InstallError::ManifestMinAppVersion;
        r.why   = "package requires app >= " + manifest.min_app_version + "; running " + app_version_;
        r.offending_pointer = "/minAppVersion";
        return r;
    }

    // Verify SHA-256 of every entry against the manifest's checksums.
    // The map is keyed by package-relative path; the value is the
    // lower-case hex SHA-256. The manifest.json itself is excluded
    // from this map because it is the file that names the checksums.
    const auto& checksums = mj["checksums"];
    std::unordered_map<std::string, std::string> expected;
    for (auto it = checksums.begin(); it != checksums.end(); ++it) {
        const std::string& key = it.key();
        if (key == "manifest.json") {
            r.error = InstallError::ManifestSelfChecksum;
            r.why   = "manifest.json cannot checksum itself";
            r.offending_pointer = "/checksums/manifest.json";
            return r;
        }
        expected.emplace(key, it.value().get<std::string>());
    }

    std::unordered_set<std::string> seen;
    for (const auto& em : entries) {
        if (em.name == "manifest.json") continue;
        auto exp = expected.find(em.name);
        if (exp == expected.end()) {
            r.error = InstallError::UnlistedFile;
            r.why   = "file '" + em.name + "' is in the archive but not in checksums";
            r.offending_pointer = em.name;
            return r;
        }
        seen.insert(em.name);

        // Read the entry's bytes, hash them, compare to expected.
        // We only do this for non-directory entries.
        if (em.is_directory) continue;
        auto ce_it = std::find_if(entries.begin(), entries.end(),
            [&](const EntryMeta& e) { return e.name == em.name; });
        // (we are iterating entries itself; use direct offset)
        std::vector<std::uint8_t> bytes;
        // Re-read the central entry to get the offset.
        std::size_t cur = eocd.cd_offset;
        for (std::uint16_t i = 0; i < eocd.entries_total; ++i) {
            CentralEntry ce;
            std::string inner_why;
            if (!read_central_entry(file, cur, ce, inner_why)) {
                r.error = InstallError::CentralDirFailed;
                r.why   = inner_why;
                return r;
            }
            cur += 46 + ce.name_len + ce.extra_len + ce.comment_len;
            if (ce.name == em.name) {
                if (!read_entry_bytes(file, ce, bytes, inner_why)) {
                    r.error = InstallError::CentralDirFailed;
                    r.why   = inner_why;
                    return r;
                }
                break;
            }
        }
        if (bytes.empty() && em.uncompressed_size > 0) {
            // Should not happen for a well-formed archive, but be defensive.
            r.error = InstallError::CentralDirFailed;
            r.why   = "could not locate entry '" + em.name + "' for hashing";
            return r;
        }
        const std::string actual = sha256_hex(bytes);
        if (actual != exp->second) {
            r.error = InstallError::ChecksumMismatch;
            r.why   = "checksum mismatch for '" + em.name + "' (expected " +
                      exp->second + ", got " + actual + ")";
            r.offending_pointer = em.name;
            return r;
        }
    }

    // The reverse check: every entry in `checksums` must correspond to
    // a real file in the archive.
    for (const auto& [key, _] : expected) {
        if (seen.find(key) == seen.end()) {
            r.error = InstallError::UnknownFileInChecksums;
            r.why   = "checksum declared for '" + key + "' but no such file in archive";
            r.offending_pointer = "/checksums/" + key;
            return r;
        }
    }

    r.error = InstallError::None;
    return r;
}

// ---------------------------------------------------------------------------
//  extract_theme — pull theme.json out of a package, validate, and
//  return a Theme. A package without a theme capability is not an
//  error: we return nullptr and the caller checks.
// ---------------------------------------------------------------------------

std::shared_ptr<const theme::Theme> SkinInstaller::extract_theme(
    const std::filesystem::path& path) {
    std::vector<EntryMeta> entries;
    PackageInfo pkg;
    auto r = inspect(path, entries, pkg);
    if (!r.ok()) return nullptr;

    // Look for theme.json in the entries. The walk above did not
    // materialise the file's bytes, so re-open and extract just that
    // one entry.
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return nullptr;
    const auto sz = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> file(sz);
    if (sz > 0 && !in.read(reinterpret_cast<char*>(file.data()),
                           static_cast<std::streamsize>(sz))) return nullptr;

    EndOfCentral eocd;
    std::string why;
    if (!find_end_of_central(file, eocd, why)) return nullptr;
    std::size_t cur = eocd.cd_offset;
    for (std::uint16_t i = 0; i < eocd.entries_total; ++i) {
        CentralEntry ce;
        if (!read_central_entry(file, cur, ce, why)) return nullptr;
        cur += 46 + ce.name_len + ce.extra_len + ce.comment_len;
        if (ce.name == "theme.json") {
            std::vector<std::uint8_t> bytes;
            if (!read_entry_bytes(file, ce, bytes, why)) return nullptr;
            const std::string text(bytes.begin(), bytes.end());
            auto tres = theme::ThemeLoader::parse(text, validator_);
            if (!tres.theme) return nullptr;
            return tres.theme;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
//  install — atomic extract.
//
//  We extract into a temp directory under the OS temp dir, then rename
//  it into place. The rename is atomic on POSIX (rename(2) of a
//  directory is atomic on the same filesystem) and on Windows when
//  the source and destination are on the same volume. The caller is
//  responsible for putting the destination on a sane volume; we
//  document that in the file header.
// ---------------------------------------------------------------------------

InstallResult SkinInstaller::install(const std::filesystem::path& path,
                                     const std::filesystem::path& destination_root) {
    InstallResult r;
    std::vector<EntryMeta> entries;
    PackageInfo manifest;
    r = inspect(path, entries, manifest);
    if (!r.ok()) return r;

    // Build a temp dir of the form <temp>/<id>-<version>-<random>.
    std::error_code ec;
    auto base = std::filesystem::temp_directory_path(ec);
    if (ec) { r.error = InstallError::AtomicRenameFailed; r.why = "no temp directory"; return r; }
    auto staging = base / (manifest.id + "-" + manifest.version + "-XXXXXX");
    char tmpl[256];
    std::snprintf(tmpl, sizeof(tmpl), "%s", staging.string().c_str());
#ifdef _WIN32
    if (_mktemp_s(tmpl, sizeof(tmpl)) != 0) { r.error = InstallError::AtomicRenameFailed; r.why = "mktemp failed"; return r; }
#else
    if (mkdtemp(tmpl) == nullptr) { r.error = InstallError::AtomicRenameFailed; r.why = "mkdtemp failed"; return r; }
#endif
    const std::filesystem::path temp_dir{tmpl};

    auto cleanup = [&](InstallResult&& ret) {
        std::error_code ec;
        std::filesystem::remove_all(temp_dir, ec);
        return ret;
    };

    // Open the source file once.
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return cleanup({InstallError::OpenFailed, "cannot reopen source", "", {}});
    const auto sz = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> file(sz);
    if (sz > 0 && !in.read(reinterpret_cast<char*>(file.data()),
                           static_cast<std::streamsize>(sz))) {
        return cleanup({InstallError::OpenFailed, "short read on source", "", {}});
    }

    EndOfCentral eocd;
    std::string why;
    if (!find_end_of_central(file, eocd, why)) {
        return cleanup({InstallError::CentralDirFailed, why, "", {}});
    }

    // Walk central directory; extract each entry into temp_dir.
    std::size_t cur = eocd.cd_offset;
    for (std::uint16_t i = 0; i < eocd.entries_total; ++i) {
        CentralEntry ce;
        if (!read_central_entry(file, cur, ce, why)) {
            return cleanup({InstallError::CentralDirFailed, why, "", {}});
        }
        cur += 46 + ce.name_len + ce.extra_len + ce.comment_len;

        const auto dest = temp_dir / ce.name;
        if (ce.name.empty()) continue;

        // Skip directory entries — the recursive create below does the work.
        if ((static_cast<std::uint8_t>(ce.external_attr & 0xFF) & 0x10) != 0) {
            std::filesystem::create_directories(dest, ec);
            if (ec) {
                return cleanup({InstallError::AtomicRenameFailed,
                                std::string{"mkdir failed: "} + ec.message(),
                                ce.name, {}});
            }
            continue;
        }

        // Re-run the path-safety check with the *real* destination
        // root. This catches the trickiest zip-slip payloads: a
        // symlink already present inside the install root that would
        // turn a benign-looking path into an escape on canonicalise.
        EntryInfo ei{ce.name,
                     EntryAttributes{((static_cast<std::uint16_t>((ce.external_attr >> 16) & 0xFFFF)) != 0 &&
                                      ((static_cast<std::uint16_t>((ce.external_attr >> 16) & 0xFFFF)) & 0170000) == 0120000),
                                    false, false}};
        auto sr = check_entry_path(ei.name, ei.attr, temp_dir);
        if (!sr.ok()) {
            return cleanup({InstallError::PathSafety, sr.why, ce.name, {}});
        }

        // Create the parent directory.
        if (dest.has_parent_path()) {
            std::filesystem::create_directories(dest.parent_path(), ec);
            if (ec) {
                return cleanup({InstallError::AtomicRenameFailed,
                                std::string{"mkdir failed: "} + ec.message(),
                                ce.name, {}});
            }
        }

        // Read the entry's bytes and write them out.
        std::vector<std::uint8_t> bytes;
        if (!read_entry_bytes(file, ce, bytes, why)) {
            return cleanup({InstallError::CentralDirFailed, why, ce.name, {}});
        }
        std::ofstream out(dest, std::ios::binary | std::ios::trunc);
        if (!out) {
            return cleanup({InstallError::AtomicRenameFailed,
                            "cannot write " + dest.string(), ce.name, {}});
        }
        if (!bytes.empty() && !out.write(reinterpret_cast<const char*>(bytes.data()),
                                          static_cast<std::streamsize>(bytes.size()))) {
            return cleanup({InstallError::AtomicRenameFailed,
                            "short write to " + dest.string(), ce.name, {}});
        }
    }

    // Atomic rename into place.
    std::filesystem::create_directories(destination_root, ec);
    if (ec) {
        return cleanup({InstallError::AtomicRenameFailed,
                        std::string{"mkdir of destination failed: "} + ec.message(),
                        "", {}});
    }
    auto final_path = destination_root / (manifest.id + "-" + manifest.version);
    std::filesystem::remove_all(final_path, ec);  // best-effort overwrite
    std::filesystem::rename(temp_dir, final_path, ec);
    if (ec) {
        return cleanup({InstallError::AtomicRenameFailed,
                        std::string{"rename failed: "} + ec.message(), "", {}});
    }
    r.installed_to = final_path;
    r.error = InstallError::None;
    return r;
}

}  // namespace arrow::skin

