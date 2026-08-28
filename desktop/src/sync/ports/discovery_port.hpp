// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Sync discovery port — spec §18.2, REQ-SYN-005.
//
// mDNS / DNS-SD advertisement of `_arrow-sync._tcp` and browse for peers on
// the local network. Discovery is disableable independently of sync, so
// the port owns its own enable bit. The actual mDNS machinery (Avahi on
// Linux, Bonjour on Windows/macOS, the platform's NSNetService on
// Android) is left to a follow-up adapter; this translation unit ships a
// pure-C++20 in-process simulator that the unit tests and headless CI can
// drive, and a stub service when the build is configured without an mDNS
// backend.

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"

namespace arrow::sync {

/// A peer that the browser has found.
struct PeerDescriptor final {
    std::string name;            ///< human-readable, e.g. "Office Mac"
    std::string uuid;            ///< device UUID (REQ-SYN-005)
    std::string host;            ///< dotted-quad or hostname
    std::uint16_t port{0};
    std::string protocol_version;///< REQ-SYN-005
    /// Lower-case hex SHA-256 fingerprint of the long-term public key
    /// advertised in the TXT record. Empty if the peer has not paired
    /// yet. REQ-SYN-005 names this so a UI can render a comparison code
    /// before the user types the 6-digit pairing code.
    std::string public_key_fingerprint;
};

struct DiscoveryCallbacks final {
    std::function<void(const PeerDescriptor&)> on_peer_found;
    std::function<void(const PeerDescriptor&)> on_peer_lost;
    std::function<void(const Error&)> on_error;
};

/// The discovery port. The host instantiates one of these and tells it
/// whether to advertise or browse (or both, though §18.2 names them as
/// independent flags).
class IDiscovery {
  public:
    virtual ~IDiscovery() = default;

    /// Start. If `advertise` is true, register our own service; if
    /// `browse` is true, watch for peers. Both may be true at once.
    [[nodiscard]] virtual Status start(bool advertise, bool browse) = 0;

    /// Stop everything. Idempotent.
    virtual void stop() noexcept = 0;

    /// Wire callbacks.
    virtual void set_callbacks(DiscoveryCallbacks callbacks) noexcept = 0;

    /// Snapshot of the current peer set. Cheap; intended for the UI.
    [[nodiscard]] virtual std::vector<PeerDescriptor> peers() const = 0;

    /// The service type. REQ-SYN-005 pins it to `_arrow-sync._tcp`.
    [[nodiscard]] static constexpr std::string_view service_type() noexcept {
        return "_arrow-sync._tcp";
    }
};

/// Factory. `device_name`, `device_uuid` and `protocol_version` populate
/// the TXT record (REQ-SYN-005); `port` is the TCP port the sync
/// transport will listen on.
[[nodiscard]] std::unique_ptr<IDiscovery> make_default_discovery(
    std::string device_name, std::string device_uuid,
    std::string protocol_version, std::uint16_t port);

}  // namespace arrow::sync
