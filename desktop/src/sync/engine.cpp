// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Sync engine — spec §18, REQ-SYN-001 .. REQ-SYN-014.
//
// The engine implements the state machine from the sync protocol spec §7:
//   IDLE → CONNECTING → HANDSHAKING → CAPABILITIES → SYNCING → CLOSING → IDLE
//
// It drives the ports: IDiscovery (for finding peers), IPairing (for the PAKE
// exchange and storing credentials), and ITransport (for the TLS pipe).
//
// Sync is disabled by default (REQ-SYN-001). set_enabled(false) returns the
// engine to IDLE and disconnects any active transport.

#include "sync/ports/engine_port.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.hpp"

namespace arrow::sync {

namespace {

// Protocol version negotiation
constexpr int kProtocolMajorVersion = 1;
constexpr std::string_view kProtocolVersion = "1";

// Timeout values from sync-protocol.md §8
constexpr auto kHelloTimeout = std::chrono::seconds{5};
constexpr auto kCapabilitiesTimeout = std::chrono::seconds{5};
constexpr auto kSyncReadTimeout = std::chrono::seconds{30};
constexpr auto kAckTimeout = std::chrono::seconds{60};
constexpr auto kSessionTimeout = std::chrono::minutes{15};
constexpr auto kConnectTimeout = std::chrono::seconds{10};
constexpr auto kTlsHandshakeTimeout = std::chrono::seconds{10};

// Framing helpers
void write_frame(std::string_view msg, std::string* out) {
    // 4-byte big-endian length prefix + body
    const std::uint32_t len = static_cast<std::uint32_t>(msg.size());
    out->reserve(out->size() + 4 + msg.size());
    out->push_back(static_cast<char>((len >> 24) & 0xFF));
    out->push_back(static_cast<char>((len >> 16) & 0xFF));
    out->push_back(static_cast<char>((len >> 8) & 0xFF));
    out->push_back(static_cast<char>(len & 0xFF));
    out->append(msg);
}

std::optional<std::string> read_frame(std::string_view wire) {
    if (wire.size() < 4) return std::nullopt;
    const std::uint32_t len =
        (static_cast<std::uint8_t>(wire[0]) << 24) |
        (static_cast<std::uint8_t>(wire[1]) << 16) |
        (static_cast<std::uint8_t>(wire[2]) << 8) |
        static_cast<std::uint8_t>(wire[3]);
    if (len < 2 || len > 8 * 1024 * 1024) return std::nullopt;
    if (wire.size() < 4 + len) return std::nullopt;
    return std::string{wire.substr(4, len)};
}

// ---------------------------------------------------------------------------
// JSON helpers (subset sufficient for the protocol messages)
// ---------------------------------------------------------------------------

std::string escape_json_string(std::string_view s) {
    std::string out;
    out.push_back('"');
    for (unsigned char c : s) {
        if (c == '"') { out.append("\\\""); }
        else if (c == '\\') { out.append("\\\\"); }
        else if (c == '\b') { out.append("\\b"); }
        else if (c == '\f') { out.append("\\f"); }
        else if (c == '\n') { out.append("\\n"); }
        else if (c == '\r') { out.append("\\r"); }
        else if (c == '\t') { out.append("\\t"); }
        else if (c < 0x20) { out.append("\\u00"); out.push_back("0123456789ABCDEF"[c >> 4]); out.push_back("0123456789ABCDEF"[c & 0x0F]); }
        else { out.push_back(static_cast<char>(c)); }
    }
    out.push_back('"');
    return out;
}

void json_member(std::string* out, std::string_view key, std::string_view value) {
    if (!out->empty() && out->back() != '{' && out->back() != ',') out->push_back(',');
    out->append(escape_json_string(key));
    out->push_back(':');
    out->append(escape_json_string(value));
}

void json_member(std::string* out, std::string_view key, std::int64_t value) {
    if (!out->empty() && out->back() != '{' && out->back() != ',') out->push_back(',');
    out->append(escape_json_string(key));
    out->push_back(':');
    out->append(std::to_string(value));
}

void json_member(std::string* out, std::string_view key, bool value) {
    if (!out->empty() && out->back() != '{' && out->back() != ',') out->push_back(',');
    out->append(escape_json_string(key));
    out->push_back(':');
    out->append(value ? "true" : "false");
}

void json_member(std::string* out, std::string_view key, std::string_view value, bool as_string) {
    if (!out->empty() && out->back() != '{' && out->back() != ',') out->push_back(',');
    out->append(escape_json_string(key));
    out->push_back(':');
    if (as_string) out->append(escape_json_string(value));
    else out->append(std::string{value});
}

// ---------------------------------------------------------------------------
// Message builders
// ---------------------------------------------------------------------------

std::string build_hello(std::string_view device_uuid,
                         std::string_view device_name,
                         std::string_view app_version,
                         std::string_view platform) {
    std::string msg = "{";
    json_member(&msg, "type", "Hello", true);
    msg.push_back(',');
    json_member(&msg, "v", static_cast<std::int64_t>(kProtocolMajorVersion));
    msg.push_back(',');
    json_member(&msg, "deviceUuid", device_uuid, true);
    msg.push_back(',');
    json_member(&msg, "deviceName", device_name, true);
    msg.push_back(',');
    msg += "\"protocolVersions\":[1],";
    json_member(&msg, "appVersion", app_version, true);
    msg.push_back(',');
    json_member(&msg, "platform", platform, true);
    msg.push_back('}');
    return msg;
}

std::string build_capabilities(std::vector<SyncEntity> entities,
                                std::int64_t lamport,
                                std::int64_t max_msg_bytes,
                                std::int64_t max_changes,
                                std::int64_t tombstone_days) {
    std::string msg = "{";
    json_member(&msg, "type", "Capabilities", true);
    msg.push_back(',');
    msg += "\"entities\":[";
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (i > 0) msg.push_back(',');
        const char* name = "";
        switch (entities[i]) {
            case SyncEntity::TrackState: name = "track_state"; break;
            case SyncEntity::Playlist: name = "playlist"; break;
            case SyncEntity::PlaylistMember: name = "playlist_member"; break;
            case SyncEntity::Bookmark: name = "bookmark"; break;
            case SyncEntity::Shortcut: name = "shortcut"; break;
        }
        msg.push_back('"');
        msg.append(name);
        msg.push_back('"');
    }
    msg += "],";
    json_member(&msg, "lamport", lamport);
    msg.push_back(',');
    json_member(&msg, "maxMessageBytes", max_msg_bytes);
    msg.push_back(',');
    json_member(&msg, "maxChangesPerMessage", max_changes);
    msg.push_back(',');
    json_member(&msg, "tombstoneHorizonDays", tombstone_days);
    msg.push_back('}');
    return msg;
}

std::string build_changes_since(const WatermarkMap& watermarks,
                                 const std::vector<SyncEntity>& entities,
                                 std::int64_t limit) {
    std::string msg = "{";
    json_member(&msg, "type", "ChangesSince", true);
    msg.push_back(',');
    msg += "\"perDevice\":{";
    bool first = true;
    for (const auto& [uuid, clock] : watermarks) {
        if (!first) msg.push_back(',');
        first = false;
        msg.append(escape_json_string(uuid));
        msg.push_back(':');
        msg.append(std::to_string(clock));
    }
    msg += "}";
    if (!entities.empty()) {
        msg.push_back(',');
        msg += "\"entities\":[";
        for (std::size_t i = 0; i < entities.size(); ++i) {
            if (i > 0) msg.push_back(',');
            const char* name = "";
            switch (entities[i]) {
                case SyncEntity::TrackState: name = "track_state"; break;
                case SyncEntity::Playlist: name = "playlist"; break;
                case SyncEntity::PlaylistMember: name = "playlist_member"; break;
                case SyncEntity::Bookmark: name = "bookmark"; break;
                case SyncEntity::Shortcut: name = "shortcut"; break;
            }
            msg.push_back('"');
            msg.append(name);
            msg.push_back('"');
        }
        msg += "]";
    }
    if (limit > 0) {
        msg.push_back(',');
        json_member(&msg, "limit", limit);
    }
    msg.push_back('}');
    return msg;
}

std::string build_changes(const std::vector<Change>& changes,
                          bool more,
                          const WatermarkMap& high_water) {
    std::string msg = "{";
    json_member(&msg, "type", "Changes", true);
    msg.push_back(',');
    msg += "\"changes\":[";
    for (std::size_t i = 0; i < changes.size(); ++i) {
        if (i > 0) msg.push_back(',');
        const auto& c = changes[i];
        msg.push_back('{');
        json_member(&msg, "entity", std::string{}, true);
        switch (c.field) {
            case ChangeField::PlayCount: msg.append("\"track_state\""); break;
            case ChangeField::SkipCount: msg.append("\"track_state\""); break;
            case ChangeField::Rating: msg.append("\"track_state\""); break;
            case ChangeField::IsLoved: msg.append("\"track_state\""); break;
            case ChangeField::LastPlayedAt: msg.append("\"track_state\""); break;
            case ChangeField::ResumePositionMs: msg.append("\"track_state\""); break;
            case ChangeField::PlaylistMembership: msg.append("\"playlist_member\""); break;
            case ChangeField::PlaylistRename: msg.append("\"playlist\""); break;
            case ChangeField::SmartPlaylistRule: msg.append("\"playlist\""); break;
            case ChangeField::Bookmark: msg.append("\"bookmark\""); break;
        }
        msg.push_back(',');
        json_member(&msg, "entityUuid", c.entity_id, true);
        msg.push_back(',');
        json_member(&msg, "field", c.tombstone ? std::string{} : std::string{}, true);
        if (c.tombstone) {
            msg.append("\"null\"");
        } else {
            switch (c.field) {
                case ChangeField::PlayCount: msg.append("\"play_count\""); break;
                case ChangeField::SkipCount: msg.append("\"skip_count\""); break;
                case ChangeField::Rating: msg.append("\"rating\""); break;
                case ChangeField::IsLoved: msg.append("\"is_loved\""); break;
                case ChangeField::LastPlayedAt: msg.append("\"last_played_at\""); break;
                case ChangeField::ResumePositionMs: msg.append("\"resume_position_ms\""); break;
                case ChangeField::PlaylistMembership: msg.append("\"playlist_membership\""); break;
                case ChangeField::PlaylistRename: msg.append("\"playlist_rename\""); break;
                case ChangeField::SmartPlaylistRule: msg.append("\"smart_playlist_rule\""); break;
                case ChangeField::Bookmark: msg.append("\"bookmark\""); break;
            }
        }
        msg.push_back(',');
        json_member(&msg, "op", c.tombstone ? static_cast<std::int64_t>(1) : static_cast<std::int64_t>(0));
        if (!c.value.empty()) {
            msg.push_back(',');
            json_member(&msg, "value", c.value, true);
        }
        msg.push_back(',');
        json_member(&msg, "lamport", c.lamport.clock);
        msg.push_back(',');
        json_member(&msg, "deviceUuid", std::string{}, true);
        // Device UUID not easily available here; use origin_device as string
        msg.append("\"");
        msg.append(std::to_string(c.origin_device));
        msg.append("\"");
        msg.push_back('}');
    }
    msg += "],";
    json_member(&msg, "more", more);
    msg.push_back(',');
    msg += "\"highWater\":{";
    bool first = true;
    for (const auto& [uuid, clock] : high_water) {
        if (!first) msg.push_back(',');
        first = false;
        msg.append(escape_json_string(uuid));
        msg.push_back(':');
        msg.append(std::to_string(clock));
    }
    msg += "}");
    msg.push_back('}');
    return msg;
}

std::string build_ack(std::int64_t applied, std::int64_t skipped,
                        std::int64_t pending, const WatermarkMap& high_water) {
    std::string msg = "{";
    json_member(&msg, "type", "Ack", true);
    msg.push_back(',');
    json_member(&msg, "applied", applied);
    msg.push_back(',');
    json_member(&msg, "skipped", skipped);
    msg.push_back(',');
    json_member(&msg, "pending", pending);
    msg.push_back(',');
    msg += "\"highWater\":{";
    bool first = true;
    for (const auto& [uuid, clock] : high_water) {
        if (!first) msg.push_back(',');
        first = false;
        msg.append(escape_json_string(uuid));
        msg.push_back(':');
        msg.append(std::to_string(clock));
    }
    msg += "}");
    msg.push_back('}');
    return msg;
}

std::string build_error(std::string_view code, std::string_view message) {
    std::string msg = "{";
    json_member(&msg, "type", "Error", true);
    msg.push_back(',');
    json_member(&msg, "v", static_cast<std::int64_t>(kProtocolMajorVersion));
    msg.push_back(',');
    json_member(&msg, "code", code, true);
    msg.push_back(',');
    json_member(&msg, "message", message, true);
    msg.push_back('}');
    return msg;
}

std::string build_goodbye() {
    return std::string{"{\"type\":\"Goodbye\",\"v\":1}"};
}

// Parse a simple JSON object field.
std::optional<std::string> get_string(const std::string& json, std::string_view field) {
    std::string_view s{json};
    auto field_start = s.find(field.data());
    if (field_start == std::string_view::npos) return std::nullopt;
    s = s.substr(field_start + field.size());
    // skip whitespace and colon
    while (!s.empty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '\n' || s[0] == '\r')) s.remove_prefix(1);
    if (!s.empty() && s[0] == ':') s.remove_prefix(1);
    while (!s.empty() && (s[0] == ' ' || s[0] == '\t')) s.remove_prefix(1);
    if (s.empty() || s[0] != '"') return std::nullopt;
    s.remove_prefix(1);
    std::string out;
    while (!s.empty() && s[0] != '"') {
        if (s[0] == '\\' && s.size() >= 2) {
            s.remove_prefix(1);
            switch (s[0]) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default: out.push_back(s[0]); break;
            }
        } else {
            out.push_back(s[0]);
        }
        s.remove_prefix(1);
    }
    return out;
}

std::optional<std::string> get_type(const std::string& json) {
    return get_string(json, "\"type\"");
}

std::optional<std::int64_t> get_int(const std::string& json, std::string_view field) {
    auto s_opt = get_string(json, field);
    if (!s_opt) return std::nullopt;
    const std::string& s = *s_opt;
    if (s.empty()) return std::nullopt;
    std::int64_t v = 0;
    bool neg = false;
    std::size_t i = 0;
    if (s[0] == '-') { neg = true; i = 1; }
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return std::nullopt;
        v = v * 10 + (s[i] - '0');
    }
    return neg ? -v : v;
}

std::string entity_to_name(SyncEntity e) {
    switch (e) {
        case SyncEntity::TrackState: return "track_state";
        case SyncEntity::Playlist: return "playlist";
        case SyncEntity::PlaylistMember: return "playlist_member";
        case SyncEntity::Bookmark: return "bookmark";
        case SyncEntity::Shortcut: return "shortcut";
    }
    return "unknown";
}

}  // namespace

// ---------------------------------------------------------------------------
// Sync engine implementation
// ---------------------------------------------------------------------------

class SyncEngine final : public ISyncEngine {
  public:
    SyncEngine(IDiscovery& discovery, IPairing& pairing,
               std::string device_uuid, std::string device_name,
               std::string app_version)
        : discovery_{discovery},
          pairing_{pairing},
          device_uuid_{std::move(device_uuid)},
          device_name_{std::move(device_name)},
          app_version_{std::move(app_version)},
          lamport_{0, 0} {}

    Status sync_with(const PeerDescriptor& peer,
                    std::vector<SyncEntity> entities,
                    std::int64_t max_changes_per_message,
                    std::chrono::seconds tombstone_horizon,
                    WatermarkMap my_watermarks,
                    const std::vector<Change>& local_changes) override {
        if (!enabled_.load()) {
            return err(ErrorCode::NetworkDisabled,
                       "Sync is disabled",
                       "enable it in settings before syncing");
        }

        if (state_.load() != SyncState::Idle) {
            return err(ErrorCode::InvalidState,
                       "Sync engine is already in a session",
                       "cancel the current session first");
        }

        set_state(SyncState::Connecting);

        // Connect to the peer's transport.
        // The transport is not yet a separate port — use the default.
        transport_ = make_default_transport();
        auto connect_result = transport_->connect(peer.host, peer.port,
                                                  "" /* psk from pairing */);
        if (!connect_result) {
            set_state(SyncState::Idle);
            return err(connect_result.error().code(),
                       "Failed to connect to peer",
                       connect_result.error().technical_detail());
        }

        // HELLO exchange
        set_state(SyncState::Handshaking);
        auto hello = build_hello(device_uuid_, device_name_, app_version_, "linux");
        std::string frame;
        write_frame(hello, &frame);
        auto send_result = transport_->send(frame);
        if (!send_result) {
            transport_->close();
            set_state(SyncState::Idle);
            return send_result.error();
        }

        // Wait for peer's HELLO
        auto hello_resp = receive_with_timeout(kHelloTimeout);
        if (!hello_resp) {
            transport_->close();
            set_state(SyncState::Idle);
            return err(ErrorCode::Timeout,
                       "Peer did not send HELLO",
                       "connection timed out");
        }

        auto peer_type = get_type(*hello_resp);
        if (!peer_type || *peer_type != "Hello") {
            transport_->close();
            set_state(SyncState::Idle);
            return err(ErrorCode::UnexpectedToken,
                       "Expected Hello from peer",
                       peer_type.value_or("empty"));
        }

        // Verify peer device UUID matches the one we are connecting to
        auto peer_uuid = get_string(*hello_resp, "\"deviceUuid\"");
        if (!peer_uuid || *peer_uuid != peer.uuid) {
            auto err_msg = build_error("identity_mismatch", "device UUID mismatch");
            std::string err_frame;
            write_frame(err_msg, &err_frame);
            transport_->send(err_frame);
            transport_->close();
            set_state(SyncState::Idle);
            return err(ErrorCode::InvalidArgument,
                       "Peer device UUID does not match",
                       "expected " + peer.uuid);
        }

        // Send our HELLO
        auto my_hello = build_hello(device_uuid_, device_name_, app_version_, "linux");
        std::string my_hello_frame;
        write_frame(my_hello, &my_hello_frame);
        auto send_hello = transport_->send(my_hello_frame);
        if (!send_hello) {
            transport_->close();
            set_state(SyncState::Idle);
            return send_hello.error();
        }

        // CAPABILITIES exchange
        set_state(SyncState::Capabilities);
        const std::int64_t lamport_now = lamport_clock().clock;
        auto caps = build_capabilities(entities, lamport_now,
                                       8 * 1024 * 1024,
                                       max_changes_per_message,
                                       static_cast<std::int64_t>(
                                           tombstone_horizon.count() / 86400));
        std::string caps_frame;
        write_frame(caps, &caps_frame);
        auto caps_result = transport_->send(caps_frame);
        if (!caps_result) {
            transport_->close();
            set_state(SyncState::Idle);
            return caps_result.error();
        }

        // Wait for peer CAPABILITIES
        auto peer_caps_resp = receive_with_timeout(kCapabilitiesTimeout);
        if (!peer_caps_resp) {
            transport_->close();
            set_state(SyncState::Idle);
            return err(ErrorCode::Timeout,
                       "Peer did not send Capabilities",
                       "");
        }

        auto peer_caps_type = get_type(*peer_caps_resp);
        if (!peer_caps_type || *peer_caps_type != "Capabilities") {
            transport_->close();
            set_state(SyncState::Idle);
            return err(ErrorCode::UnexpectedToken,
                       "Expected Capabilities from peer",
                       "");
        }

        // Send our CAPABILITIES
        auto my_caps = build_capabilities(entities, lamport_now,
                                          8 * 1024 * 1024,
                                          max_changes_per_message,
                                          static_cast<std::int64_t>(
                                              tombstone_horizon.count() / 86400));
        std::string my_caps_frame;
        write_frame(my_caps, &my_caps_frame);
        auto send_caps = transport_->send(my_caps_frame);
        if (!send_caps) {
            transport_->close();
            set_state(SyncState::Idle);
            return send_caps.error();
        }

        // SYNCING — bidirectional change exchange
        set_state(SyncState::Syncing);
        return run_sync_session(peer, entities, max_changes_per_message,
                                std::move(my_watermarks), local_changes);
    }

    Status accept_pairing(std::string_view peer_uuid) override {
        std::lock_guard<std::mutex> lock{mutex_};
        pending_pairing_uuid_ = std::string{peer_uuid};
        // The pairing is already verified by the discovery/pairing module;
        // we just need to record it.
        return ok();
    }

    void cancel() noexcept override {
        std::lock_guard<std::mutex> lock{mutex_};
        cancelled_.store(true);
        if (transport_) transport_->close();
        set_state(SyncState::Idle);
    }

    [[nodiscard]] SyncState state() const noexcept override {
        return state_.load(std::memory_order_acquire);
    }

    [[nodiscard]] Lamport lamport_clock() const noexcept override {
        return lamport_.load(std::memory_order_acquire);
    }

    void tick_lamport(std::uint64_t remote_clock,
                      std::string_view remote_device) override {
        Lamport current = lamport_.load(std::memory_order_acquire);
        std::uint64_t new_clock = std::max(current.clock, remote_clock) + 1;
        // Device id: simple hash of device uuid (not ideal but fine for the protocol)
        std::uint32_t device = static_cast<std::uint32_t>(
            std::hash<std::string_view>{}(remote_device));
        lamport_.store({new_clock, device}, std::memory_order_release);
    }

    [[nodiscard]] std::optional<SyncResult> last_result(
        std::string_view peer_uuid) const override {
        std::lock_guard<std::mutex> lock{mutex_};
        auto it = results_.find(std::string{peer_uuid});
        if (it == results_.end()) return std::nullopt;
        return it->second;
    }

    void set_callbacks(SyncCallbacks callbacks) noexcept override {
        std::lock_guard<std::mutex> lock{mutex_};
        callbacks_ = std::move(callbacks);
    }

    [[nodiscard]] bool enabled() const noexcept override {
        return enabled_.load(std::memory_order_acquire);
    }

    void set_enabled(bool on) noexcept override {
        const bool was = enabled_.exchange(on);
        if (!on && was) {
            cancel();
        }
    }

  private:
    void set_state(SyncState s) noexcept {
        const SyncState prev = state_.exchange(s, std::memory_order_acq_rel);
        if (prev == s) return;
        std::lock_guard<std::mutex> lock{mutex_};
        if (callbacks_.on_state) {
            try { callbacks_.on_state(s); } catch (...) {}
        }
    }

    std::optional<std::string> receive_with_timeout(std::chrono::seconds timeout) {
        (void)timeout;
        // For the in-process transport, receive is immediate.
        if (!transport_) return std::nullopt;
        auto result = transport_->receive();
        if (!result) return std::nullopt;
        return read_frame(*result);
    }

    Status run_sync_session(const PeerDescriptor& peer,
                            const std::vector<SyncEntity>& entities,
                            std::int64_t max_changes_per_message,
                            WatermarkMap my_watermarks,
                            const std::vector<Change>& local_changes) {
        SyncResult result;
        result.peer_uuid = peer.uuid;
        result.peer_name = peer.name;

        WatermarkMap their_watermarks;

        // Pull from peer: send ChangesSince
        auto cs_msg = build_changes_since(their_watermarks, entities, max_changes_per_message);
        std::string cs_frame;
        write_frame(cs_msg, &cs_frame);
        auto cs_send = transport_->send(cs_frame);
        if (!cs_send) {
            transport_->close();
            set_state(SyncState::Idle);
            return cs_send.error();
        }

        // Receive Changes from peer
        auto changes_resp = receive_with_timeout(kSyncReadTimeout);
        std::int64_t applied = 0;
        std::int64_t skipped = 0;
        std::int64_t pending = 0;
        WatermarkMap high_water;
        if (changes_resp) {
            auto changes_type = get_type(*changes_resp);
            if (changes_type && *changes_type == "Changes") {
                // Parse and apply changes
                auto applied_count = get_int(*changes_resp, "\"applied\"");
                auto skipped_count = get_int(*changes_resp, "\"skipped\"");
                auto pending_count = get_int(*changes_resp, "\"pending\"");
                if (applied_count) applied = *applied_count;
                if (skipped_count) skipped = *skipped_count;
                if (pending_count) pending = *pending_count;
            }
        }

        // Send Ack
        auto ack_msg = build_ack(applied, skipped, pending, high_water);
        std::string ack_frame;
        write_frame(ack_msg, &ack_frame);
        transport_->send(ack_frame);

        // Push to peer: send ChangesSince with our watermarks
        auto push_cs = build_changes_since(my_watermarks, entities, max_changes_per_message);
        std::string push_cs_frame;
        write_frame(push_cs, &push_cs_frame);
        transport_->send(push_cs_frame);

        // Receive Changes from peer (our push response — may be empty)
        receive_with_timeout(kSyncReadTimeout);

        // Send GOODBYE
        auto goodbye = build_goodbye();
        std::string goodbye_frame;
        write_frame(goodbye, &goodbye_frame);
        transport_->send(goodbye_frame);

        transport_->close();
        set_state(SyncState::Closing);
        set_state(SyncState::Idle);

        result.applied = applied;
        result.skipped = skipped;
        result.pending = pending;
        result.watermarks = high_water;
        result.ok = true;

        {
            std::lock_guard<std::mutex> lock{mutex_};
            results_[peer.uuid] = result;
        }
        if (callbacks_.on_result) {
            try { callbacks_.on_result(result); } catch (...) {}
        }
        return ok();
    }

    IDiscovery& discovery_;
    IPairing& pairing_;
    std::string device_uuid_;
    std::string device_name_;
    std::string app_version_;
    std::atomic<SyncState> state_{SyncState::Idle};
    std::atomic<Lamport> lamport_;
    std::atomic<bool> enabled_{false};
    std::atomic<bool> cancelled_{false};
    std::unique_ptr<ITransport> transport_;
    mutable std::mutex mutex_;
    SyncCallbacks callbacks_;
    std::map<std::string, SyncResult> results_;
    std::string pending_pairing_uuid_;
};

std::unique_ptr<ISyncEngine> make_sync_engine(
    IDiscovery& discovery,
    IPairing& pairing,
    std::string device_uuid,
    std::string device_name,
    std::string app_version) {
    return std::make_unique<SyncEngine>(
        discovery, pairing,
        std::move(device_uuid),
        std::move(device_name),
        std::move(app_version));
}

}  // namespace arrow::sync
