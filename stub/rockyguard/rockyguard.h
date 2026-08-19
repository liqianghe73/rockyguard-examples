// RockyGuard API STUB -- not RockyGuard, and not a reimplementation of it.
//
// WHY THIS EXISTS
// The RockyGuard SDK is proprietary and cannot be committed to a public repo or
// exposed to pull requests from forks (GitHub does not give fork PRs access to
// repository secrets). Without a stub, every outside contribution would show red
// CI, and the first thing a prospect saw would be a failing build.
//
// So this header mirrors the SDK's public API surface exactly, and always
// reports "no license". The examples compile, run, and are testable against it,
// exercising the unlicensed code path -- which is the path most users hit first
// and the one most likely to be wrong.
//
// IT NEVER GRANTS ANYTHING. There is deliberately no environment variable or
// build flag that makes the stub report a valid license. A repo published by a
// license-compliance vendor must not contain a switch that appears to unlock
// paid features. To see the unlocked behaviour, build against the real SDK with
// a real license: -DROCKYGUARD_ROOT=/path/to/sdk
//
// Kept in sync with rockyguard/*.h v1.3.2 by tests/api_canary.cpp, which
// instantiates every symbol the examples use. If the SDK renames something, that
// test fails loudly rather than the gate silently disabling itself.

#pragma once

#include <map>
#include <string>
#include <vector>

#define ROCKYGUARD_API
#define ROCKYGUARD_STUB 1

#define ROCKYGUARD_VERSION_MAJOR 1
#define ROCKYGUARD_VERSION_MINOR 3
#define ROCKYGUARD_VERSION_PATCH 2
#define ROCKYGUARD_VERSION_STRING "1.3.2-stub"

namespace rockyguard {

constexpr int VERSION_MAJOR = ROCKYGUARD_VERSION_MAJOR;
constexpr int VERSION_MINOR = ROCKYGUARD_VERSION_MINOR;
constexpr int VERSION_PATCH = ROCKYGUARD_VERSION_PATCH;
constexpr const char* VERSION_STRING = ROCKYGUARD_VERSION_STRING;

enum class LicenseType { NodeLocked, Floating };

// Mirrors the SDK enum, in the SDK's order. NotYetValid sits mid-enum in the
// real header and is never returned; it is reproduced here so that any code
// switching exhaustively behaves identically against stub and SDK.
enum class LicenseStatus {
    Valid,
    Expired,
    InGracePeriod,
    HardwareMismatch,
    SignatureInvalid,
    MalformedFile,
    NotYetValid,
    FeatureNotLicensed,
    NoLicensesAvailable,
    ServerUnreachable,
    LibraryNotInitialized,
    TierNotAuthorized,
    GenerationLimitReached,
    MachineNotAuthorized,
    ClockManipulated,
    IntegrityCheckFailed,
    MachineSeatLimitReached,
    VersionMismatch,
    KeyMalformed,
    KeyChecksumMismatch
};

enum class SignatureAlgorithm { RSA_SHA256, Ed25519, AutoDetect };

struct ROCKYGUARD_API LicenseResult {
    // Defaults to a FAILING status, exactly as the SDK does. Valid is
    // enumerator 0, so a defaulted result must not be Valid or a forgotten
    // assignment would fail open.
    LicenseStatus status = LicenseStatus::MalformedFile;
    std::string message;
    int grace_days_remaining = 0;

    explicit operator bool() const {
        return status == LicenseStatus::Valid || status == LicenseStatus::InGracePeriod;
    }
};

struct ROCKYGUARD_API License {
    static constexpr int kMaxGraceDays = 36500;
    static constexpr int kEvaluationMintDays = 7;

    std::string license_id;
    std::string licensee;
    std::string product;
    std::string version_range;
    LicenseType type = LicenseType::NodeLocked;
    std::string hardware_fingerprint;
    int fingerprint_match_threshold = 2;
    std::string issued_at;
    std::string expires_at;
    int grace_period_days = 0;
    int max_concurrent_users = 0;
    std::vector<std::string> features;
    std::map<std::string, std::string> metadata;

    bool is_expired() const { return true; }
    bool is_in_grace_period() const { return false; }
    int grace_days_remaining() const { return 0; }
    bool has_feature(const std::string&) const { return false; }
    bool is_evaluation_mint() const { return false; }
};

class ROCKYGUARD_API LicenseVerifier {
public:
    explicit LicenseVerifier(const std::string& /*public_key_pem*/,
                             SignatureAlgorithm /*algo*/ = SignatureAlgorithm::AutoDetect) {}
    ~LicenseVerifier() = default;

    // Non-copyable, like the real class. Code that compiles against the stub
    // must compile against the SDK, so the stub may not be more permissive.
    LicenseVerifier(const LicenseVerifier&) = delete;
    LicenseVerifier& operator=(const LicenseVerifier&) = delete;

    LicenseResult load(const std::string&) { return unlicensed(); }
    LicenseResult load_from_string(const std::string&) { return unlicensed(); }
    LicenseResult check_node_locked() { return unlicensed(); }
    LicenseResult check_feature(const std::string&) { return notLicensed(); }
    LicenseResult check_expiry() { return unlicensed(); }
    LicenseResult check_version(const std::string&) const { return unlicensed(); }

    License license() const { return License{}; }
    bool is_loaded() const { return false; }

private:
    static LicenseResult unlicensed() {
        LicenseResult r;
        r.status = LicenseStatus::MalformedFile;
        r.message = "built against the RockyGuard API stub, not the SDK -- "
                    "no license can be verified. Configure with "
                    "-DROCKYGUARD_ROOT=/path/to/sdk to use the real library.";
        return r;
    }
    static LicenseResult notLicensed() {
        LicenseResult r = unlicensed();
        r.status = LicenseStatus::FeatureNotLicensed;
        return r;
    }
};

}  // namespace rockyguard
