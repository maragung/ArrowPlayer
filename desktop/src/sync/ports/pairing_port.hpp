// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Pairing port — spec §18.2, REQ-SYN-006.
//
// The PAKE (Password-Authenticated Key Exchange) is the part that turns a
// 6-digit code the user typed into a long-term shared key. SPAKE2 is the
// algorithm the spec names ("SPAKE2 or equivalent PAKE"); this port
// provides the state machine — 6-digit code generation and validation,
// 120-second expiry, 5-attempt lockout with 5-minute cooldown — and
// delegates the actual PAKE math to a backend.
//
// The pure-C++20 backend lives in pairing.cpp; the production backend
// (libsodium, OpenSSL, BoringSSL — whichever the §4.2 register names) is
// expected to be wired in by the build once it is selected.

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "core/error.hpp"
#include "net/ports/scrobble_port.hpp"  // for ISecretStore

namespace arrow::sync {

/// State of a pairing attempt. The state machine is documented in
/// §18.2: the code is shown on device A, typed on device B, both sides
/// run the PAKE, and either both transition to Paired or both return to
/// Idle on a verification failure.
enum class PairingState {
    Idle,
    AwaitingCode,     ///< device A: showing the 6-digit code
    AwaitingPeer,     ///< device B: code typed, waiting for the peer
    Verifying,        ///< PAKE in flight
    Paired,           ///< success — long-term key in the secret store
    LockedOut         ///< 5 attempts exhausted; 5-minute cooldown
};

struct PairingCallbacks final {
    std::function<void(PairingState)> on_state;
    std::function<void(const Error&)> on_error;
    /// A new 6-digit code was generated. Device A's UI listens to this.
    std::function<void(std::string code)> on_code;
    /// A successful pairing produced a peer record. The host uses this
    /// to populate the device list.
    std::function<void(std::string peer_uuid, std::string peer_name,
                       std::string public_key_fingerprint)>
        on_paired;
};

/// Parameters for the PAKE. Defaults match REQ-SYN-006 exactly:
///   6-digit code, 120 s expiry, 5 attempts, 5-minute cooldown.
struct PairingPolicy final {
    int code_digits{6};
    std::chrono::seconds code_ttl{120};
    int max_attempts{5};
    std::chrono::seconds cooldown{std::chrono::minutes{5}};
};

/// The pairing port.
class IPairing {
  public:
    virtual ~IPairing() = default;

    /// Begin acting as the code-displaying device. Returns the generated
    /// code via the on_code callback (and the same string via the
    /// optional out parameter, for synchronous callers).
    [[nodiscard]] virtual Status start_displaying_code(
        std::string* out_code = nullptr) = 0;

    /// Begin acting as the code-typing device.
    [[nodiscard]] virtual Status start_accepting_code(
        std::string_view code) = 0;

    /// Cancel an in-progress pairing attempt. Idempotent.
    virtual void cancel() noexcept = 0;

    /// Wipe a paired device. Removes the long-term key from the secret
    /// store; revocation must take effect immediately (REQ-SYN-007).
    [[nodiscard]] virtual Status forget_peer(std::string_view peer_uuid) = 0;

    /// The policy. Mutable for tests; production callers should not
    /// override the defaults.
    [[nodiscard]] virtual PairingPolicy policy() const noexcept = 0;

    /// State. Cheap.
    [[nodiscard]] virtual PairingState state() const noexcept = 0;

    /// Remaining attempts before the cooldown kicks in. -1 means
    /// currently locked out.
    [[nodiscard]] virtual int remaining_attempts() const noexcept = 0;

    /// Wire callbacks.
    virtual void set_callbacks(PairingCallbacks callbacks) noexcept = 0;
};

/// Factory. The host provides the secret store the long-term key is
/// written to, and the device UUID this pairing session is for.
[[nodiscard]] std::unique_ptr<IPairing> make_default_pairing(
    net::ISecretStore& secrets, std::string device_uuid,
    PairingPolicy policy = {});

/// Helper for the test surface: turn a 32-byte raw key into the
/// lower-case hex fingerprint the discovery TXT record carries
/// (REQ-SYN-005). The pure-C++20 SHA-256 implementation lives in
/// pairing.cpp and is shared with the rest of the sync module.
[[nodiscard]] std::string fingerprint_hex(std::string_view raw_public_key);

}  // namespace arrow::sync
