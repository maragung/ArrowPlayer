// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Sync discovery — spec §18.2, REQ-SYN-005.
//
// The production backend is platform-mDNS: Avahi on Linux, Bonjour on
// Windows / macOS, NSNetService on Android. Each lives in a separate
// adapter translation unit; this file is the cross-platform C++20
// implementation that ships in every build, used for unit tests and as
// the fallback when no mDNS stack is available at runtime.
//
// The implementation is deliberately small. Real mDNS is a multicast UDP
// protocol with a per-record cache and a re-query schedule; we are not
// here to ship a competitor to Avahi. The on-the-wire contract is
// `_arrow-sync._tcp` with a TXT record carrying
//   uuid=<device uuid>
//   ver=<protocol version>
//   fp=<lower-case hex SHA-256 fingerprint of the long-term public key>
// and a friendly instance name.

#include "sync/ports/discovery_port.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.hpp"
#include "core/text.hpp"

namespace arrow::sync {

namespace {

/// Split a TXT record ("a=1;b=2;c=3") into key-value pairs. Used both
/// for parsing the local record and for decoding peer records seen on
/// the wire.
[[maybe_unused]] std::map<std::string, std::string> parse_txt(
    std::string_view s) {
    std::map<std::string, std::string> out;
    while (!s.empty()) {
        const std::size_t semi = s.find(';');
        std::string_view part = s.substr(0, semi);
        s = semi == std::string_view::npos ? std::string_view{}
                                           : s.substr(semi + 1);
        const std::size_t eq = part.find('=');
        if (eq == std::string_view::npos) continue;
        out.emplace(std::string{part.substr(0, eq)},
                    std::string{part.substr(eq + 1)});
    }
    return out;
}

std::string build_txt(const std::string& uuid, const std::string& ver,
                      const std::string& fingerprint) {
    std::string out;
    out.append("uuid=").append(uuid).append(";");
    out.append("ver=").append(ver).append(";");
    if (!fingerprint.empty()) {
        out.append("fp=").append(fingerprint);
    }
    return out;
}

}  // namespace

class InProcessDiscovery final : public IDiscovery {
  public:
    InProcessDiscovery(std::string name, std::string uuid, std::string ver,
                       std::uint16_t port)
        : name_{std::move(name)}, uuid_{std::move(uuid)}, ver_{std::move(ver)},
          port_{port} {}

    Status start(bool advertise, bool browse) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (advertise) advertising_ = true;
        if (browse) browsing_ = true;
        running_ = advertising_ || browsing_;
        if (running_) {
            txt_ = build_txt(uuid_, ver_, fingerprint_);
        }
        return ok();
    }

    void stop() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        advertising_ = false;
        browsing_ = false;
        running_ = false;
        peers_.clear();
    }

    void set_callbacks(DiscoveryCallbacks callbacks) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_ = std::move(callbacks);
    }

    std::vector<PeerDescriptor> peers() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<PeerDescriptor> out;
        out.reserve(peers_.size());
        for (const auto& [_, p] : peers_) out.push_back(p);
        return out;
    }

    /// Test / inter-process integration hook: a host can simulate a peer
    /// appearing on the LAN by calling this. In a real build, the
    /// platform-mDNS adapter is what populates the same map.
    void inject_peer(const PeerDescriptor& peer) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (peer.uuid == uuid_) return;
        const bool is_new = peers_.find(peer.uuid) == peers_.end();
        peers_[peer.uuid] = peer;
        if (is_new && callbacks_.on_peer_found) {
            try {
                callbacks_.on_peer_found(peer);
            } catch (...) {
            }
        }
    }

    /// Test hook: drop a peer as if the mDNS entry had expired.
    void drop_peer(const std::string& peer_uuid) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = peers_.find(peer_uuid);
        if (it == peers_.end()) return;
        const PeerDescriptor copy = it->second;
        peers_.erase(it);
        if (callbacks_.on_peer_lost) {
            try {
                callbacks_.on_peer_lost(copy);
            } catch (...) {
            }
        }
    }

    /// Set the local fingerprint. Called by the pairing module once a
    /// long-term keypair has been generated so the TXT record reflects
    /// the current device's identity.
    void set_fingerprint(std::string fp) {
        std::lock_guard<std::mutex> lock(mutex_);
        fingerprint_ = std::move(fp);
        txt_ = build_txt(uuid_, ver_, fingerprint_);
    }

    [[nodiscard]] const std::string& local_txt() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return txt_;
    }

  private:
    mutable std::mutex mutex_;
    std::string name_;
    std::string uuid_;
    std::string ver_;
    std::string fingerprint_;
    std::string txt_;
    std::uint16_t port_;
    bool advertising_{false};
    bool browsing_{false};
    bool running_{false};
    DiscoveryCallbacks callbacks_;
    std::map<std::string, PeerDescriptor> peers_;
};

std::unique_ptr<IDiscovery> make_default_discovery(
    std::string device_name, std::string device_uuid,
    std::string protocol_version, std::uint16_t port) {
    return std::make_unique<InProcessDiscovery>(
        std::move(device_name), std::move(device_uuid),
        std::move(protocol_version), port);
}

}  // namespace arrow::sync
