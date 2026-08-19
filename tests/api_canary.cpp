// API canary.
//
// This does not test behaviour. It touches every RockyGuard symbol the examples
// depend on, so that an SDK rename or signature change breaks the BUILD loudly
// instead of silently disabling a license check.
//
// The failure mode being guarded against is specific and nasty: if
// check_feature() were renamed and the gate quietly fell back to a default, the
// examples would still compile, still run, and would report the wrong tier. A
// compile-time canary turns that into a red build.
//
// It also keeps stub/rockyguard/rockyguard.h honest -- the stub must expose the
// same surface, with the same const-ness and the same copy semantics, or this
// file fails against one of the two.

#include <rockyguard/rockyguard.h>

#include <cstdio>
#include <exception>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#ifdef _WIN32
// _set_abort_behavior / SetErrorMode / _CrtSetReportMode, so an abort() in a test
// writes to stderr instead of opening a modal dialog that hangs CI forever.
#include <windows.h>

#include <crtdbg.h>
#include <cstdlib>
#endif

using namespace rockyguard;

// --- version macros and constants ------------------------------------------
static_assert(ROCKYGUARD_VERSION_MAJOR >= 1, "unexpected major version");
static_assert(VERSION_MAJOR >= 1, "namespace version constant missing");
static_assert(std::is_same<decltype(VERSION_STRING), const char* const>::value,
              "VERSION_STRING should be a const char*");

// --- LicenseResult ----------------------------------------------------------
// Valid is enumerator 0, which is the whole reason the SDK gives
// LicenseResult::status a non-Valid default: a struct without an initialiser
// would come out Valid and convert to true -- fail-OPEN, one forgotten
// assignment away from an unlicensed run.
static_assert(static_cast<int>(LicenseStatus::Valid) == 0,
              "Valid must stay enumerator 0 -- the fail-closed default depends on it");

// The matching "a defaulted result is falsy" check has to be a RUNTIME check,
// not a static_assert: LicenseResult holds a std::string, so it is not a
// literal type and cannot be constructed in a constant expression. Verified in
// main() instead -- see failClosed().
static bool failClosed() { return !static_cast<bool>(LicenseResult{}); }

// operator bool is explicit in the SDK. If it ever became implicit, sloppy
// integrator code would start compiling; catch that here.
static_assert(!std::is_convertible<LicenseResult, bool>::value,
              "LicenseResult::operator bool must stay explicit");

// LicenseVerifier owns a unique_ptr<Impl> and is deliberately non-copyable.
// Qt code that captures it in a lambda by value must fail to compile.
static_assert(!std::is_copy_constructible<LicenseVerifier>::value,
              "LicenseVerifier must remain non-copyable");
static_assert(!std::is_copy_assignable<LicenseVerifier>::value,
              "LicenseVerifier must remain non-copy-assignable");

// --- enum surface -----------------------------------------------------------
// Named exhaustively so that a removed or reordered enumerator is a hard error.
// The SDK's own header warns that NotYetValid sits mid-enum and must not be
// deleted for tidiness, because removing it renumbers every later value.
static void enumSurface() {
    const LicenseStatus all[] = {
        LicenseStatus::Valid,
        LicenseStatus::Expired,
        LicenseStatus::InGracePeriod,
        LicenseStatus::HardwareMismatch,
        LicenseStatus::SignatureInvalid,
        LicenseStatus::MalformedFile,
        LicenseStatus::NotYetValid,
        LicenseStatus::FeatureNotLicensed,
        LicenseStatus::NoLicensesAvailable,
        LicenseStatus::ServerUnreachable,
        LicenseStatus::LibraryNotInitialized,
        LicenseStatus::TierNotAuthorized,
        LicenseStatus::GenerationLimitReached,
        LicenseStatus::MachineNotAuthorized,
        LicenseStatus::ClockManipulated,
        LicenseStatus::IntegrityCheckFailed,
        LicenseStatus::MachineSeatLimitReached,
        LicenseStatus::VersionMismatch,
        LicenseStatus::KeyMalformed,
        LicenseStatus::KeyChecksumMismatch,
    };
    (void)all;
    (void)LicenseType::NodeLocked;
    (void)LicenseType::Floating;
    (void)SignatureAlgorithm::RSA_SHA256;
    (void)SignatureAlgorithm::Ed25519;
    (void)SignatureAlgorithm::AutoDetect;
}

// --- verifier and license surface ------------------------------------------
// NEVER CALLED at runtime. This function exists to be COMPILED -- that is the
// whole point of a compile-time canary. Executing it would mean feeding a
// deliberately malformed PEM to the real library, which is not something this
// test has any business doing.
//
// It used to be called from main(), and against the real SDK that terminated the
// process with 0xC0000409 and no output. See probeBadKey() below for what
// actually happens and why it matters.
static void verifierSurface() {
    LicenseVerifier v("-----BEGIN PUBLIC KEY-----\nnot-a-key\n-----END PUBLIC KEY-----\n");
    LicenseVerifier explicitAlgo("pem", SignatureAlgorithm::Ed25519);

    LicenseResult r = v.load("nope.lic");
    r = v.load_from_string("{}");
    r = v.check_node_locked();
    r = v.check_feature("cad_3d");
    r = v.check_expiry();
    r = v.check_version("1.0.0");

    (void)r.status;
    (void)r.message;
    (void)r.grace_days_remaining;
    (void)static_cast<bool>(r);
    (void)v.is_loaded();

    // Returns BY VALUE since v1.3. `auto&` must NOT compile; binding to const&
    // extends the temporary's lifetime and must.
    const License byValue = v.license();
    const License& boundToTemp = v.license();
    static_assert(std::is_same<decltype(v.license()), License>::value,
                  "license() must return License by value");

    (void)byValue.license_id;
    (void)byValue.licensee;
    (void)byValue.product;
    (void)byValue.version_range;
    (void)byValue.type;
    (void)byValue.hardware_fingerprint;
    (void)byValue.fingerprint_match_threshold;
    (void)byValue.issued_at;
    (void)byValue.expires_at;
    (void)byValue.grace_period_days;
    (void)byValue.max_concurrent_users;
    (void)byValue.features;
    (void)byValue.metadata;
    (void)byValue.has_feature("cad_3d");
    (void)byValue.is_expired();
    (void)byValue.is_in_grace_period();
    (void)byValue.grace_days_remaining();
    (void)byValue.is_evaluation_mint();
    (void)License::kMaxGraceDays;
    (void)License::kEvaluationMintDays;
    (void)boundToTemp.licensee;

    // No const in these: decltype on a member-access expression yields the
    // member's DECLARED type, not the type adjusted for the object's constness.
    // `const License byValue` still gives decltype(byValue.features) ==
    // std::vector<std::string>.
    static_assert(std::is_same<decltype(byValue.features), std::vector<std::string>>::value,
                  "License::features should be a vector<string>");
    static_assert(
        std::is_same<decltype(byValue.metadata), std::map<std::string, std::string>>::value,
        "License::metadata should be a map<string,string>");
}

// On Windows, abort() and a failed CRT assertion open a MODAL DIALOG and block
// until a human clicks it. On a CI runner nobody clicks, so the job hangs until
// the whole build times out -- which is precisely what happened here before this
// function existed. Route all of it to stderr and let the process die.
static void suppressCrashDialogs() {
#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#ifdef _DEBUG
    const int reports[] = {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT};
    for (int r : reports) {
        _CrtSetReportMode(r, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(r, _CRTDBG_FILE_STDERR);
    }
#endif
#endif
}

// Diagnostic, OPT-IN ONLY: run with --probe-bad-key.
//
// What the SDK does when handed a public key it cannot parse, measured against
// v1.3.2 with MSVC 19.44:
//
//   Release (/MD, rockyguard.lib)     -> THROWS std::runtime_error
//        "Failed to parse public key PEM:
//         error:1E08010C:DECODER routines::unsupported"
//        Catchable, and a genuinely good message.
//
//   Debug (/MDd, rockyguard_mdd.lib)  -> calls abort()
//        NOT catchable. On Windows that is a modal "abort() has been called"
//        dialog, so a developer pressing F5 with a mistyped key gets a box no
//        catch block can intercept, and an unattended run hangs indefinitely.
//
// That asymmetry is why this probe is off by default, and why 01-minimal wraps
// the constructor in try/catch: the Release behaviour is recoverable and worth
// demonstrating, and the Debug behaviour must never run unattended.
static void probeBadKey() {
    const char* kGarbage = "-----BEGIN PUBLIC KEY-----\nnot-a-key\n-----END PUBLIC KEY-----\n";
    std::printf("probe: constructing LicenseVerifier with a malformed PEM ... ");
    std::fflush(stdout);
    try {
        LicenseVerifier v(kGarbage);
        const LicenseResult r = v.load("nonexistent.lic");
        std::printf("no throw; load() -> status=%d msg=\"%s\"\n", static_cast<int>(r.status),
                    r.message.c_str());
    } catch (const std::exception& e) {
        std::printf("THREW std::exception: %s\n", e.what());
    } catch (...) {
        std::printf("THREW a non-std exception\n");
    }
}

int main(int argc, char** argv) {
    suppressCrashDialogs();

    enumSurface();
    // verifierSurface() is deliberately NOT called -- it exists to be compiled.

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--probe-bad-key") {
            probeBadKey();
        }
    }

    if (!failClosed()) {
        std::printf("api_canary: FAIL -- a default-constructed LicenseResult is truthy.\n"
                    "            That is a fail-OPEN default: unlicensed runs would be "
                    "treated as licensed.\n");
        return 1;
    }
#ifdef ROCKYGUARD_STUB
    std::printf("api_canary: OK (built against the API STUB)\n");
#else
    std::printf("api_canary: OK (built against the SDK, v%s)\n", VERSION_STRING);
#endif
    return 0;
}

