#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace prismix::security {

// One authoritative PIN, lock and LAN-access authority shared by the LVGL
// console and every Web UI session. The PIN is only ever persisted as a salted
// yescrypt hash.
class SecurityService {
public:
    static SecurityService& instance();

    explicit SecurityService(std::string configPath = {});
    ~SecurityService();

    SecurityService(const SecurityService&) = delete;
    SecurityService& operator=(const SecurityService&) = delete;

    void initialize(std::string configPath = {});

    bool isPinConfigured() const;
    bool isMixerLocked() const;
    bool isLanAccessEnabled() const;
    std::string configPath() const;
    std::string persistentError() const;

    // PIN policy: 6..12 ASCII digits. Validation is shared by every caller.
    static bool isValidPin(const std::string& pin);
    static const char* pinPolicyText();

    bool setupPin(const std::string& pin, const std::string& confirmPin,
                  std::string& error);
    bool changePin(const std::string& currentPin, const std::string& newPin,
                   const std::string& confirmPin, const std::string& clientKey,
                   std::string& error, int& retryAfterSeconds);

    // Verifies against the salted hash and applies per-client rate limiting.
    // `retryAfterSeconds` is non-zero when the caller is temporarily blocked.
    bool verifyPin(const std::string& pin, const std::string& clientKey,
                   std::string& error, int& retryAfterSeconds);

    // Sessions are random 256-bit bearer tokens held in memory only. The raw
    // token is sent only in the HttpOnly cookie and is never persisted.
    std::string createSession();
    bool validateSession(const std::string& token, int64_t* expiresAt = nullptr) const;
    void revokeSession(const std::string& token);
    void revokeAllSessions();
    std::size_t activeSessionCount() const;
    int64_t sessionRemainingSeconds(const std::string& token) const;

    bool lockMixer(std::string& error);
    bool unlockMixer(const std::string& pin, const std::string& clientKey,
                     std::string& error, int& retryAfterSeconds);

    bool setLanAccess(bool enabled, std::string& error);

    static std::string cookieValue(const std::string& cookieHeader,
                                   const std::string& name);
    static std::string makeSessionCookie(const std::string& token,
                                         int64_t maxAgeSeconds);
    static std::string makeExpiredSessionCookie();

    // Bounded, test-only controls. Production callers never adjust the clock or
    // session lifetime through the Web API.
    void setSessionTtlForTesting(int64_t seconds);
    void resetRateLimitsForTesting();

private:
    struct AttemptState {
        int failures = 0;
        int64_t lastFailureMonotonic = 0;
        int64_t blockedUntilMonotonic = 0;
    };
    struct SessionState {
        int64_t expiresAtSystem = 0;
    };

    void loadLocked();
    bool persistLocked(std::string& error);
    bool validatePinFields(const std::string& pin, const std::string& confirmPin,
                           std::string& error) const;
    std::string hashPin(const std::string& pin) const;
    bool verifyHash(const std::string& pin, const std::string& encoded,
                    bool& validEncoding) const;
    int64_t monotonicNow() const;
    void pruneSessionsLocked(int64_t nowSystem) const;

    mutable std::mutex mutex_;
    std::string configPath_;
    std::string persistentError_;
    std::string pinHash_;
    bool pinConfigured_ = false;
    bool mixerLocked_ = false;
    bool lanAccess_ = false;
    int64_t sessionTtlSeconds_ = 8 * 60 * 60;
    std::unordered_map<std::string, AttemptState> attempts_;
    mutable std::unordered_map<std::string, SessionState> sessions_;
};

} // namespace prismix::security
