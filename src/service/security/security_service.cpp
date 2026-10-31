#include "security_service.h"

#include <crypt.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#if defined(__linux__)
#include <sys/random.h>
#endif
#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace prismix::security {

namespace {

constexpr int64_t kMaxFailureWindowSeconds = 15 * 60;
constexpr int kFreeAttempts = 5;
constexpr int64_t kInitialLockoutSeconds = 30;
constexpr int64_t kMaxLockoutSeconds = 5 * 60;

std::string hex_encode(const unsigned char* data, std::size_t size) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(digits[(data[i] >> 4) & 0x0f]);
        out.push_back(digits[data[i] & 0x0f]);
    }
    return out;
}

std::string trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string default_config_path() {
    if (const char* explicitPath = std::getenv("PRISMIX_SECURITY_CONFIG")) {
        if (explicitPath[0] != '\0') return explicitPath;
    }
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        if (xdg[0] != '\0') {
            return (std::filesystem::path(xdg) / "prismix" / "security.conf").string();
        }
    }
    if (const char* home = std::getenv("HOME")) {
        if (home[0] != '\0') {
            return (std::filesystem::path(home) / ".config" / "prismix" / "security.conf").string();
        }
    }
    return (std::filesystem::temp_directory_path() / "prismix-security.conf").string();
}

bool random_bytes(unsigned char* out, std::size_t size) {
#if defined(__linux__)
    std::size_t filled = 0;
    while (filled < size) {
        const ssize_t n = getrandom(out + filled, size - filled, 0);
        if (n <= 0) return false;
        filled += static_cast<std::size_t>(n);
    }
    return true;
#else
    std::ifstream random("/dev/urandom", std::ios::binary);
    if (!random) return false;
    random.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(size));
    return random.gcount() == static_cast<std::streamsize>(size);
#endif
}

bool constant_time_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

void secure_zero(void* data, std::size_t size) {
    volatile unsigned char* p = static_cast<volatile unsigned char*>(data);
    while (size-- > 0) *p++ = 0;
}

void secure_clear(std::string& value) {
    if (!value.empty()) {
        secure_zero(value.data(), value.size());
        value.clear();
    }
}

} // namespace

SecurityService& SecurityService::instance() {
    static SecurityService service;
    return service;
}

SecurityService::SecurityService(std::string configPath) {
    initialize(std::move(configPath));
}

SecurityService::~SecurityService() {
    std::lock_guard<std::mutex> lock(mutex_);
    secure_clear(pinHash_);
}

void SecurityService::initialize(std::string configPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    configPath_ = configPath.empty() ? default_config_path() : std::move(configPath);
    pinHash_.clear();
    pinConfigured_ = false;
    mixerLocked_ = false;
    lanAccess_ = false;
    attempts_.clear();
    sessions_.clear();
    persistentError_.clear();
    loadLocked();
}

bool SecurityService::isPinConfigured() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pinConfigured_;
}

bool SecurityService::isMixerLocked() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pinConfigured_ && mixerLocked_;
}

bool SecurityService::isLanAccessEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pinConfigured_ && lanAccess_;
}

std::string SecurityService::configPath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return configPath_;
}

std::string SecurityService::persistentError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return persistentError_;
}

const char* SecurityService::pinPolicyText() {
    return "Use 6 to 12 digits.";
}

bool SecurityService::isValidPin(const std::string& pin) {
    if (pin.size() < 6 || pin.size() > 12) return false;
    return std::all_of(pin.begin(), pin.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
}

bool SecurityService::validatePinFields(const std::string& pin,
                                        const std::string& confirmPin,
                                        std::string& error) const {
    if (pin.empty()) {
        error = "Enter a PIN.";
        return false;
    }
    if (!isValidPin(pin)) {
        error = pinPolicyText();
        return false;
    }
    if (pin != confirmPin) {
        error = "The PIN confirmation does not match.";
        return false;
    }
    return true;
}

std::string SecurityService::hashPin(const std::string& pin) const {
    // libxcrypt's yescrypt ($y$) is the memory-hard password hash shipped with
    // the platform. crypt_gensalt draws a fresh salt from the OS CSPRNG.
    char setting[CRYPT_GENSALT_OUTPUT_SIZE];
    if (crypt_gensalt_rn("$y$", 0, nullptr, 0, setting, sizeof(setting)) == nullptr) {
        return {};
    }
    struct crypt_data data{};
    const char* encoded = crypt_r(pin.c_str(), setting, &data);
    const std::string result = encoded ? std::string(encoded) : std::string{};
    secure_zero(&data, sizeof(data));
    secure_zero(setting, sizeof(setting));
    return result;
}

bool SecurityService::verifyHash(const std::string& pin, const std::string& encoded,
                                 bool& validEncoding) const {
    validEncoding = false;
    if (encoded.size() < 20 || encoded.size() > 192) return false;
    if (encoded.rfind("$y$", 0) != 0 && encoded.rfind("$gy$", 0) != 0) return false;
    struct crypt_data data{};
    const char* actual = crypt_r(pin.c_str(), encoded.c_str(), &data);
    if (!actual) {
        secure_zero(&data, sizeof(data));
        return false;
    }
    validEncoding = true;
    const bool match = constant_time_equal(encoded, actual);
    secure_zero(&data, sizeof(data));
    return match;
}

bool SecurityService::setupPin(const std::string& pin, const std::string& confirmPin,
                               std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pinConfigured_) {
        error = "A mixer PIN is already configured.";
        return false;
    }
    error.clear();
    if (!validatePinFields(pin, confirmPin, error)) return false;

    const std::string newHash = hashPin(pin);
    if (newHash.empty()) {
        error = "Could not create the PIN hash.";
        return false;
    }

    const std::string oldHash = pinHash_;
    const bool oldConfigured = pinConfigured_;
    pinHash_ = newHash;
    pinConfigured_ = true;
    mixerLocked_ = false;
    lanAccess_ = false;
    std::string persistError;
    if (!persistLocked(persistError)) {
        secure_clear(pinHash_);
        pinHash_ = oldHash;
        pinConfigured_ = oldConfigured;
        error = persistError.empty() ? "Could not save the security configuration." : persistError;
        return false;
    }
    attempts_.clear();
    sessions_.clear();
    return true;
}

bool SecurityService::changePin(const std::string& currentPin,
                                const std::string& newPin,
                                const std::string& confirmPin,
                                const std::string& clientKey,
                                std::string& error,
                                int& retryAfterSeconds) {
    retryAfterSeconds = 0;
    error.clear();
    if (!validatePinFields(newPin, confirmPin, error)) return false;

    // Changing a PIN is a PIN verification path too. Route it through the same
    // per-client rate limiter as login and unlock so an authenticated session
    // cannot be used to brute-force the current PIN.
    if (!verifyPin(currentPin, clientKey, error, retryAfterSeconds)) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!pinConfigured_) {
        error = "Set up a PIN first.";
        return false;
    }
    const std::string newHash = hashPin(newPin);
    if (newHash.empty()) {
        error = "Could not create the PIN hash.";
        return false;
    }
    const std::string oldHash = pinHash_;
    pinHash_ = newHash;
    std::string persistError;
    if (!persistLocked(persistError)) {
        secure_clear(pinHash_);
        pinHash_ = oldHash;
        error = persistError.empty() ? "Could not save the new PIN." : persistError;
        return false;
    }
    attempts_.clear();
    sessions_.clear();
    return true;
}

int64_t SecurityService::monotonicNow() const {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool SecurityService::verifyPin(const std::string& pin, const std::string& clientKey,
                                std::string& error, int& retryAfterSeconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    retryAfterSeconds = 0;
    error.clear();
    if (!pinConfigured_) {
        error = "No mixer PIN is configured.";
        return false;
    }

    const std::string key = clientKey.empty() ? "unknown" : clientKey;
    const int64_t now = monotonicNow();
    AttemptState& attempt = attempts_[key];

    if (attempt.blockedUntilMonotonic > now) {
        retryAfterSeconds = static_cast<int>(std::max<int64_t>(1, attempt.blockedUntilMonotonic - now));
        error = "Too many incorrect attempts. Try again in " +
                std::to_string(retryAfterSeconds) + " seconds.";
        return false;
    }
    if (attempt.lastFailureMonotonic > 0 &&
        now - attempt.lastFailureMonotonic > kMaxFailureWindowSeconds) {
        attempt = AttemptState{};
    }

    bool validEncoding = false;
    const bool match = verifyHash(pin, pinHash_, validEncoding);
    if (match) {
        attempts_.erase(key);
        return true;
    }

    if (!validEncoding) {
        error = "The stored PIN configuration is invalid.";
        return false;
    }

    ++attempt.failures;
    attempt.lastFailureMonotonic = now;
    if (attempt.failures >= kFreeAttempts) {
        const int64_t exponent = std::min<int64_t>(attempt.failures - kFreeAttempts, 4);
        const int64_t delay = std::min<int64_t>(kInitialLockoutSeconds * (1LL << exponent),
                                                kMaxLockoutSeconds);
        attempt.blockedUntilMonotonic = now + delay;
        retryAfterSeconds = static_cast<int>(delay);
        error = "Too many incorrect attempts. Try again in " +
                std::to_string(retryAfterSeconds) + " seconds.";
    } else {
        const int remaining = kFreeAttempts - attempt.failures;
        error = "Incorrect PIN. " + std::to_string(remaining) +
                (remaining == 1 ? " attempt remaining." : " attempts remaining.");
    }
    return false;
}

std::string SecurityService::createSession() {
    unsigned char raw[32];
    if (!random_bytes(raw, sizeof(raw))) return {};
    const std::string token = hex_encode(raw, sizeof(raw));
    secure_zero(raw, sizeof(raw));
    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const int64_t expiry = now + sessionTtlSeconds_;
    pruneSessionsLocked(now);
    sessions_[token] = SessionState{expiry};
    return token;
}

void SecurityService::pruneSessionsLocked(int64_t nowSystem) const {
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second.expiresAtSystem <= nowSystem) it = sessions_.erase(it);
        else ++it;
    }
}

bool SecurityService::validateSession(const std::string& token, int64_t* expiresAt) const {
    if (token.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    pruneSessionsLocked(now);
    const auto it = sessions_.find(token);
    if (it == sessions_.end()) return false;
    if (expiresAt) *expiresAt = it->second.expiresAtSystem;
    return true;
}

void SecurityService::revokeSession(const std::string& token) {
    if (token.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(token);
}

void SecurityService::revokeAllSessions() {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.clear();
}

std::size_t SecurityService::activeSessionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    pruneSessionsLocked(std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count());
    return sessions_.size();
}

int64_t SecurityService::sessionRemainingSeconds(const std::string& token) const {
    if (token.empty()) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    auto it = sessions_.find(token);
    if (it == sessions_.end()) return 0;
    return std::max<int64_t>(0, it->second.expiresAtSystem - now);
}

bool SecurityService::lockMixer(std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if (!pinConfigured_) {
        error = "Set up a mixer PIN before locking the console.";
        return false;
    }
    mixerLocked_ = true;
    sessions_.clear();
    if (!persistLocked(error)) {
        mixerLocked_ = false;
        return false;
    }
    return true;
}

bool SecurityService::unlockMixer(const std::string& pin, const std::string& clientKey,
                                  std::string& error, int& retryAfterSeconds) {
    // verifyPin owns the mutex, so do not hold it across this call.
    if (!verifyPin(pin, clientKey, error, retryAfterSeconds)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mixerLocked_) return true;
    mixerLocked_ = false;
    if (!persistLocked(error)) {
        mixerLocked_ = true;
        return false;
    }
    return true;
}

bool SecurityService::setLanAccess(bool enabled, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if (enabled && !pinConfigured_) {
        error = "PIN protection must be configured before LAN access is enabled.";
        return false;
    }
    if (lanAccess_ == enabled) return true;
    lanAccess_ = enabled;
    if (!persistLocked(error)) {
        lanAccess_ = !enabled;
        return false;
    }
    return true;
}

std::string SecurityService::cookieValue(const std::string& cookieHeader,
                                         const std::string& name) {
    std::size_t start = 0;
    while (start < cookieHeader.size()) {
        std::size_t end = cookieHeader.find(';', start);
        if (end == std::string::npos) end = cookieHeader.size();
        std::string item = trim(cookieHeader.substr(start, end - start));
        const std::size_t eq = item.find('=');
        if (eq != std::string::npos) {
            if (trim(item.substr(0, eq)) == name) return trim(item.substr(eq + 1));
        }
        start = end + 1;
    }
    return {};
}

std::string SecurityService::makeSessionCookie(const std::string& token,
                                               int64_t maxAgeSeconds) {
    std::ostringstream out;
    out << "prismix_session=" << token
        << "; Path=/; HttpOnly; SameSite=Strict; Max-Age="
        << std::max<int64_t>(0, maxAgeSeconds);
    return out.str();
}

std::string SecurityService::makeExpiredSessionCookie() {
    return "prismix_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0";
}

void SecurityService::setSessionTtlForTesting(int64_t seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessionTtlSeconds_ = std::max<int64_t>(1, seconds);
}

void SecurityService::resetRateLimitsForTesting() {
    std::lock_guard<std::mutex> lock(mutex_);
    attempts_.clear();
}

void SecurityService::loadLocked() {
    std::ifstream in(configPath_);
    if (!in) return;
    std::string line;
    bool sawVersion = false;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "version") {
            sawVersion = value == "1";
        } else if (key == "pin_hash") {
            pinHash_ = value;
            // Only enable PIN protection for a structurally valid yescrypt hash
            // produced by libxcrypt. The actual comparison still happens through
            // crypt_r on every login attempt.
            pinConfigured_ = value.size() >= 20 && value.size() <= 192 &&
                             (value.rfind("$y$", 0) == 0 || value.rfind("$gy$", 0) == 0);
        } else if (key == "mixer_locked") {
            mixerLocked_ = value == "1";
        } else if (key == "lan_access") {
            lanAccess_ = value == "1";
        }
    }
    if (!sawVersion) {
        // Unknown format: fail closed for optional network exposure but keep the
        // hash usable if it was structurally valid.
        lanAccess_ = false;
    }
    if (!pinConfigured_) {
        mixerLocked_ = false;
        lanAccess_ = false;
    }
}

bool SecurityService::persistLocked(std::string& error) {
    error.clear();
    try {
        const std::filesystem::path path(configPath_);
        const std::filesystem::path parent = path.parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                error = "Could not create the security configuration directory.";
                return false;
            }
#if !defined(_WIN32)
            chmod(parent.c_str(), S_IRWXU);
#endif
        }
        const std::filesystem::path temp = path.string() + ".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) {
                error = "Could not open the security configuration for writing.";
                return false;
            }
            out << "# PRISMIX security configuration. Contains a salted yescrypt hash, never the PIN.\n";
            out << "version=1\n";
            out << "pin_hash=" << pinHash_ << "\n";
            out << "mixer_locked=" << (mixerLocked_ ? 1 : 0) << "\n";
            out << "lan_access=" << (lanAccess_ ? 1 : 0) << "\n";
            out.flush();
            if (!out) {
                error = "Could not write the security configuration.";
                return false;
            }
        }
#if !defined(_WIN32)
        chmod(temp.c_str(), S_IRUSR | S_IWUSR);
#endif
        std::error_code ec;
        std::filesystem::rename(temp, path, ec);
        if (ec) {
            std::filesystem::remove(temp, ec);
            error = "Could not install the security configuration.";
            return false;
        }
#if !defined(_WIN32)
        chmod(path.c_str(), S_IRUSR | S_IWUSR);
#endif
        persistentError_.clear();
        return true;
    } catch (const std::exception&) {
        error = "A filesystem error prevented saving the security configuration.";
        return false;
    }
}

} // namespace prismix::security
