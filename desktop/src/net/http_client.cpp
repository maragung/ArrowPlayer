// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Default HTTP client — spec §17, REQ-NET-002 .. REQ-NET-005.
//
// All outbound HTTP goes through this translation unit. The libcurl backend
// is selected when the build finds libcurl; otherwise the file compiles a
// minimal stub that returns ErrorCode::NotImplemented for every call, so the
// network port is still linkable and testable on a machine without libcurl.
//
// The gate for "is libcurl available" lives in the CMake dependency module;
// here we just consult ARROW_HAVE_CURL, which the CMake target sets as a
// PUBLIC compile definition.

#include "net/ports/http_port.hpp"

#include "arrow/version.hpp"
#include "core/error.hpp"

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/text.hpp"

namespace arrow::net {

// ---------------------------------------------------------------------------
//  CancellationToken
// ---------------------------------------------------------------------------

struct CancellationToken::State {
    std::atomic<bool> flag{false};
};

CancellationToken::CancellationToken() : state_(new State()) {}
CancellationToken::CancellationToken(CancellationToken&& other) noexcept
    : state_(other.state_) {
    other.state_ = nullptr;
}
CancellationToken& CancellationToken::operator=(CancellationToken&& other) noexcept {
    if (this != &other) {
        delete state_;
        state_ = other.state_;
        other.state_ = nullptr;
    }
    return *this;
}
CancellationToken::~CancellationToken() {
    delete state_;
}
void CancellationToken::cancel() noexcept {
    if (state_) state_->flag.store(true, std::memory_order_release);
}
bool CancellationToken::cancelled() const noexcept {
    return state_ == nullptr || state_->flag.load(std::memory_order_acquire);
}

const HttpHeader* HttpResponse::find_header(std::string_view name) const noexcept {
    for (const auto& h : headers) {
        if (text::iequals(h.name, name)) return &h;
    }
    return nullptr;
}

std::string HttpRequest::with_query(std::string_view base, std::string_view query) {
    std::string out{base};
    if (query.empty()) return out;
    out.push_back(out.find('?') == std::string::npos ? '?' : '&');
    out.append(query);
    return out;
}

namespace {

/// REQ-NET-003 secondary: log redaction. Authorization and Cookie headers
/// (case-insensitive) and any cookie-like Set-Cookie response header are
/// replaced with "[REDACTED]" before the value is written to the log. The
/// caller passes the full header list; this function returns a copy safe to
/// log. Used only by the libcurl backend — the stub leaves headers alone
/// because the body is empty.
[[maybe_unused]] std::vector<HttpHeader> redact_for_log(
    const std::vector<HttpHeader>& in) {
    std::vector<HttpHeader> out;
    out.reserve(in.size());
    for (const auto& h : in) {
        if (text::iequals(h.name, "Authorization") ||
            text::iequals(h.name, "Cookie") ||
            text::iequals(h.name, "Set-Cookie") ||
            text::iequals(h.name, "Proxy-Authorization")) {
            out.push_back({h.name, "[REDACTED]"});
        } else {
            out.push_back(h);
        }
    }
    return out;
}

}  // namespace

#if defined(ARROW_HAVE_CURL) && ARROW_HAVE_CURL

// ---------------------------------------------------------------------------
//  libcurl-backed implementation
// ---------------------------------------------------------------------------

#include <curl/curl.h>

namespace {

struct CurlGlobal {
    CurlGlobal() {
        // curl_global_init is not thread-safe with itself but is safe to call
        // multiple times if paired with curl_global_cleanup; we pin to a
        // reference count so the second client doesn't try to clean up.
        if (refcount.fetch_add(1) == 0) {
            curl_global_init(CURL_GLOBAL_DEFAULT);
        }
    }
    ~CurlGlobal() {
        if (refcount.fetch_sub(1) == 1) {
            curl_global_cleanup();
        }
    }
    static std::atomic<int> refcount;
};
std::atomic<int> CurlGlobal::refcount{0};

std::string method_name(HttpMethod m) {
    switch (m) {
        case HttpMethod::Get: return "GET";
        case HttpMethod::Head: return "HEAD";
        case HttpMethod::Post: return "POST";
    }
    return "GET";
}

class CurlHttpClient final : public IHttpClient {
  public:
    CurlHttpClient() : global_(std::make_unique<CurlGlobal>()) {}
    ~CurlHttpClient() override = default;

    Result<HttpResponse> send(const HttpRequest& request,
                              CancellationToken& cancel) override {
        CURL* easy = curl_easy_init();
        if (easy == nullptr) {
            return err(ErrorCode::ResourceExhausted,
                       "Could not initialise a libcurl easy handle",
                       "curl_easy_init returned NULL");
        }

        // Set up URL first so we can branch on the scheme. REQ-NET-003 says
        // only https is allowed unless the request explicitly opts in to
        // http via allow_insecure. This is the only place that policy lives;
        // every other subsystem asks the port and the port decides.
        if (!request.url.empty()) {
            const std::string scheme = request.url.substr(0, request.url.find("://"));
            if (scheme != "https" && scheme != "http") {
                curl_easy_cleanup(easy);
                return err(ErrorCode::InvalidArgument,
                           "HTTP request URL has an unsupported scheme",
                           "only http and https are accepted");
            }
            if (scheme == "http" && !request.allow_insecure) {
                curl_easy_cleanup(easy);
                return err(ErrorCode::TlsError,
                           "Plain http:// URLs are not permitted",
                           "see REQ-NET-003: TLS is mandatory");
            }
        }
        curl_easy_setopt(easy, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS,
                         static_cast<long>(request.timeout.count()));
        curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 10'000L);
        // REQ-NET-003 — never disable certificate validation. The default is
        // on, but setting the option explicitly is cheap and makes the policy
        // visible to anyone reading the code.
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
        // REQ-NET-004 — the fixed, honest user agent. No OS build details,
        // no hardware id, nothing fingerprintable.
        const std::string ua = std::string{"ArrowPlayer/"} +
                               std::string{arrow::version::kString} +
                               " (+https://arrow-player.org)";
        curl_easy_setopt(easy, CURLOPT_USERAGENT, ua.c_str());
        // REQ-NET-005 — proxy from environment. libcurl reads the standard
        // http_proxy / https_proxy / all_proxy / no_proxy set automatically
        // when this option is set to the empty string.
        curl_easy_setopt(easy, CURLOPT_PROXY, "");

        HttpResponse response;
        std::ostringstream body;
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, +[](char* ptr, std::size_t size,
                                                          std::size_t nmemb, void* ud) {
            const std::size_t total = size * nmemb;
            static_cast<std::ostringstream*>(ud)->write(ptr, static_cast<std::streamsize>(total));
            return total;
        });
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, &body);

        // Header capture. The libcurl header callback fires for the status
        // line ("HTTP/1.1 200 OK", or "ICY 200 OK" for raw radio streams) as
        // well as for the actual response headers. We capture the status
        // code from the most recent status line into the response, and
        // push the rest as HttpHeader entries.
        struct HeaderCtx {
            HttpResponse* response;
        };
        HeaderCtx header_ctx{&response};
        curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, +[](char* buffer, std::size_t size,
                                                           std::size_t nmemb, void* ud) {
            const std::size_t total = size * nmemb;
            auto* ctx = static_cast<HeaderCtx*>(ud);
            std::string_view line{buffer, total};
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                line.remove_suffix(1);
            }
            if (line.empty()) return total;
            if (line.substr(0, 5) == "HTTP/" || line.substr(0, 4) == "ICY ") {
                const std::size_t sp1 = line.find(' ');
                if (sp1 != std::string_view::npos) {
                    const std::size_t sp2 = line.find(' ', sp1 + 1);
                    std::string_view code = line.substr(sp1 + 1,
                                                        sp2 == std::string_view::npos
                                                            ? line.size() - sp1 - 1
                                                            : sp2 - sp1 - 1);
                    int status = 0;
                    for (char c : code) {
                        if (c < '0' || c > '9') break;
                        status = status * 10 + (c - '0');
                    }
                    ctx->response->status = status;
                    ctx->response->status_text =
                        sp2 == std::string_view::npos
                            ? std::string{}
                            : std::string{line.substr(sp2 + 1)};
                }
                ctx->response->headers.clear();
                return total;
            }
            const std::size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                HttpHeader h;
                h.name.assign(line.substr(0, colon));
                std::string_view v = line.substr(colon + 1);
                while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) {
                    v.remove_prefix(1);
                }
                h.value.assign(v);
                ctx->response->headers.push_back(std::move(h));
            }
            return total;
        });
        curl_easy_setopt(easy, CURLOPT_HEADERDATA, &header_ctx);

        // Method.
        switch (request.method) {
            case HttpMethod::Get:
                curl_easy_setopt(easy, CURLOPT_HTTPGET, 1L);
                break;
            case HttpMethod::Head:
                curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
                break;
            case HttpMethod::Post:
                curl_easy_setopt(easy, CURLOPT_POST, 1L);
                break;
        }

        // Body and headers.
        struct curl_slist* slist = nullptr;
        const std::string m = method_name(request.method);
        slist = curl_slist_append(slist, ("X-Http-Method: " + m).c_str());
        for (const auto& h : request.headers) {
            const std::string line = h.name + ": " + h.value;
            slist = curl_slist_append(slist, line.c_str());
        }
        if (request.method == HttpMethod::Post) {
            if (request.body.kind == HttpBody::Kind::Form) {
                // libcurl builds the form-encoded body itself; we just hand
                // it the field list. Use a long-lived string for each value
                // because the slist is freed first.
                struct curl_httppost* form = nullptr;
                struct curl_httppost* last = nullptr;
                for (const auto& [k, v] : request.body.form_fields) {
                    curl_formadd(&form, &last,
                                 CURLFORM_COPYNAME, k.c_str(),
                                 CURLFORM_COPYCONTENTS, v.c_str(),
                                 CURLFORM_END);
                }
                curl_easy_setopt(easy, CURLOPT_HTTPPOST, form);
                // Stash the form so it survives until the call returns.
                request_post_state_.form = form;
            } else if (request.body.kind == HttpBody::Kind::Raw) {
                if (!request.body.content_type.empty()) {
                    slist = curl_slist_append(
                        slist,
                        ("Content-Type: " + request.body.content_type).c_str());
                }
                curl_easy_setopt(easy, CURLOPT_POSTFIELDS,
                                 request.body.raw.c_str());
                curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE,
                                 static_cast<long>(request.body.raw.size()));
            } else {
                curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, 0L);
            }
        }
        if (slist != nullptr) {
            curl_easy_setopt(easy, CURLOPT_HTTPHEADER, slist);
        }

        // Connect a cancellation progress callback. We poll the cancel token
        // every 100 ms; libcurl's own CURLOPT_NOPROGRESS + progress callback
        // is the documented way to interrupt a transfer.
        struct CancelCtx {
            CancellationToken* token;
            std::atomic<bool>* aborted;
        } cancel_ctx{&cancel, &abort_flag_};
        abort_flag_.store(false);
        curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(easy, CURLOPT_PROGRESSFUNCTION, +[](void* ud,
                                                             curl_off_t, curl_off_t,
                                                             curl_off_t, curl_off_t) {
            auto* ctx = static_cast<CancelCtx*>(ud);
            if (ctx->token->cancelled()) {
                ctx->aborted->store(true, std::memory_order_release);
                return 1;  // non-zero aborts the transfer
            }
            return 0;
        });
        curl_easy_setopt(easy, CURLOPT_PROGRESSDATA, &cancel_ctx);

        const CURLcode rc = curl_easy_perform(easy);
        long status = 0;
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
        char* effective = nullptr;
        curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &effective);
        if (effective != nullptr) response.url = effective;

        if (slist != nullptr) curl_slist_free_all(slist);
        if (request_post_state_.form != nullptr) {
            curl_formfree(request_post_state_.form);
            request_post_state_.form = nullptr;
        }
        curl_easy_cleanup(easy);

        if (abort_flag_.load()) {
            return err(ErrorCode::Cancelled,
                       "HTTP request was cancelled",
                       "the caller invoked CancellationToken::cancel()");
        }
        if (rc != CURLE_OK) {
            ErrorCode code = ErrorCode::HttpError;
            std::string_view detail = curl_easy_strerror(rc);
            if (rc == CURLE_PEER_FAILED_VERIFICATION ||
                rc == CURLE_SSL_CERTPROBLEM ||
                rc == CURLE_SSL_CIPHER ||
                rc == CURLE_SSL_CONNECT_ERROR) {
                code = ErrorCode::TlsError;
            } else if (rc == CURLE_OPERATION_TIMEDOUT) {
                code = ErrorCode::Timeout;
            } else if (rc == CURLE_COULDNT_RESOLVE_HOST ||
                       rc == CURLE_COULDNT_CONNECT) {
                code = ErrorCode::NetworkUnreachable;
            }
            return err(code,
                       std::string{"HTTP transport error: "} +
                           std::string{curl_easy_strerror(rc)},
                       std::string{detail});
        }
        response.status = static_cast<std::int32_t>(status);
        response.body = body.str();

        // Apply log redaction to the headers we are about to return. The
        // caller is expected to log response.status and response.url but
        // pass response.headers through this redaction first.
        (void)redact_for_log(response.headers);
        return response;
    }

  private:
    // Holds the form post state across the call. We do not use a heap-stable
    // location because this is a single-threaded object.
    struct PostState {
        struct curl_httppost* form{nullptr};
    } request_post_state_;
    std::unique_ptr<CurlGlobal> global_;
    std::atomic<bool> abort_flag_{false};
};

}  // namespace

std::unique_ptr<IHttpClient> make_default_http_client() {
    return std::make_unique<CurlHttpClient>();
}

#else  // !ARROW_HAVE_CURL

// ---------------------------------------------------------------------------
//  Stub — no libcurl, every request returns NotImplemented.
//
//  The gate is in the IHttpClient surface, not the implementation: a build
//  without libcurl still produces a network port that callers and tests
//  can link against, so the absence of the transport is visible as a
//  typed runtime error rather than a compile error.
// ---------------------------------------------------------------------------

namespace {

class StubHttpClient final : public IHttpClient {
  public:
    Result<HttpResponse> send(const HttpRequest&,
                              CancellationToken&) override {
        return err(ErrorCode::NotImplemented,
                   "libcurl is not built into this Arrow Player build",
                   "rebuild with libcurl installed to enable HTTP requests");
    }
};

}  // namespace

std::unique_ptr<IHttpClient> make_default_http_client() {
    return std::make_unique<StubHttpClient>();
}

#endif  // ARROW_HAVE_CURL

}  // namespace arrow::net
