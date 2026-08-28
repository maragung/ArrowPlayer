// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// HTTP client port — spec §17, REQ-NET-002 .. REQ-NET-005.
//
// Every outbound HTTP request in Arrow goes through this one port. The
// transport (libcurl) lives behind the port, so the policy surface — TLS
// enforcement, fixed user-agent, proxy discovery, log redaction, cancellation
// and timeouts — is enforced in exactly one place. The §23.9 firewall test
// proves no second network path exists (REQ-NET-002).

#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"

namespace arrow::net {

/// HTTP method. Only the verbs the player actually uses are named, so a typo
/// at a call site is a compile error rather than a silent fallback.
enum class HttpMethod { Get, Head, Post };

/// A single header on a request or response. Header names are stored
/// case-insensitively: HTTP/1.1 §3.2 says headers are case-insensitive, and
/// callers should never need to remember whether the response sent
/// "Content-Type" or "content-type".
struct HttpHeader {
    std::string name;
    std::string value;
};

/// Request body for POST. `form` is the typical podcast/scrobble case; raw
/// bytes are accepted for endpoints that expect JSON.
struct HttpBody {
    enum class Kind { Empty, Form, Raw };
    Kind kind{Kind::Empty};
    std::vector<std::pair<std::string, std::string>> form_fields;
    std::string raw;
    std::string content_type;  ///< only used with Kind::Raw
};

/// One outbound request.
struct HttpRequest final {
    HttpMethod method{HttpMethod::Get};
    std::string url;                              ///< must be https://, or
                                                  ///< http:// explicitly opted
                                                  ///< in via `allow_insecure`
    HttpBody body;                                ///< GET/HEAD ignore this
    std::vector<HttpHeader> headers;              ///< caller-supplied headers
    std::chrono::milliseconds timeout{std::chrono::seconds{15}};
    bool allow_insecure{false};                   ///< §17 / REQ-NET-003: only
                                                  ///< for user-typed radio URLs

    /// Convenience: parse a query string into a URL. Provided so callers do
    /// not have to percent-encode by hand for the (rare) GET-with-body case.
    static std::string with_query(std::string_view base, std::string_view query);
};

/// One inbound response.
struct HttpResponse final {
    std::int32_t status{0};                       ///< e.g. 200, 301, 404
    std::string status_text;                      ///< verbatim from the server
    std::vector<HttpHeader> headers;              ///< response headers
    std::string body;                             ///< raw response body
    std::string url;                              ///< final URL after redirects

    /// Look up a header case-insensitively. Returns nullptr if absent.
    [[nodiscard]] const HttpHeader* find_header(std::string_view name) const noexcept;
};

/// A handle a caller can use to cancel an in-flight request. Calling cancel()
/// after the request has completed is a no-op. The handle is safe to destroy
/// without calling cancel() — the destructor signals cancellation so a
/// forgotten handle never strands a worker thread.
class CancellationToken {
  public:
    CancellationToken();
    CancellationToken(const CancellationToken&) = delete;
    CancellationToken& operator=(const CancellationToken&) = delete;
    CancellationToken(CancellationToken&&) noexcept;
    CancellationToken& operator=(CancellationToken&&) noexcept;
    ~CancellationToken();

    /// Request cancellation. Idempotent. After this returns, the active
    /// IHttpClient::send() call returns Result with ErrorCode::Cancelled
    /// promptly (no further I/O on the wire).
    void cancel() noexcept;

    /// True if cancel() has been called or the handle was destroyed.
    [[nodiscard]] bool cancelled() const noexcept;

  private:
    struct State;
    State* state_;
};

/// The single HTTP client port. One process owns one of these and passes it
/// to every subsystem that needs the network (radio, podcasts, scrobble,
/// update checker, sync). The class is NOT thread-safe: the I/O thread model
/// puts each call on its own worker. Adapters must be safe to call from many
/// threads, however, so tests can fire concurrent requests against a stub.
class IHttpClient {
  public:
    virtual ~IHttpClient() = default;

    /// Issue one request. The cancel token is consulted at every I/O step.
    /// On cancellation, returns an Error with code ErrorCode::Cancelled.
    /// On TLS failure, ErrorCode::TlsError. On a non-2xx response, the
    /// response is still returned (callers inspect `status`); only transport
    /// errors short-circuit the result.
    [[nodiscard]] virtual Result<HttpResponse> send(const HttpRequest& request,
                                                     CancellationToken& cancel) = 0;
};

/// A factory that produces the default client. The libcurl-backed build is
/// the production one; tests may swap it for a stub. Returning a unique_ptr
/// keeps the implementation detail (curl_easy handle) out of the port.
[[nodiscard]] std::unique_ptr<IHttpClient> make_default_http_client();

}  // namespace arrow::net
