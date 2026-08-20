// 01-minimal -- the whole RockyGuard integration, in one file.
//
// This is the artifact behind the claim that integration is about five lines.
// The five lines are marked below. Everything else is reporting, so a stranger
// who runs it can see what the library decided and why, without reading docs.
//
// Source is deliberately ASCII-only. Non-ASCII in C++ sources is a portability
// tax on Windows: without /utf-8 MSVC reads the file in the system ANSI
// codepage, so on a GBK- or Shift-JIS-locale machine a stray em dash either
// mis-encodes or raises C4819. We pass /utf-8 anyway (see cmake/RockyGuard.cmake)
// and still keep sources ASCII, because a contributor's editor is not ours.
//
// Build: see ../../README.md
// Run:   01-minimal [path-to-license.lic]

#include <rockyguard/rockyguard.h>

#include <cstdio>
#include <exception>
#include <string>

// Your vendor public key, produced by tools/license_keygen and pasted here.
// It is a PUBLIC key: shipping it inside your binary is the intended design.
// The matching private key never leaves your build machine.
static const char* kPublicKey = R"(-----BEGIN PUBLIC KEY-----
MCowBQYDK2VwAyEAIicBg8+AY+6zuO8v5OwRXeSrQKlmMKopMYZxkAv3tVc=
-----END PUBLIC KEY-----
)";

// The two features this example gates on. Feature names are just strings in the
// license file, so they are yours to choose; keep them stable once shipped.
static const char* kFeature3D = "cad_3d";
static const char* kFeatureStl = "cad_stl_export";

// Turn a LicenseStatus into something a human can act on. Worth writing once:
// "it failed" is not an actionable support ticket, but "this license was signed
// for a different machine" is.
static const char* explain(rockyguard::LicenseStatus s) {
    using S = rockyguard::LicenseStatus;
    switch (s) {
        case S::Valid:                   return "valid";
        case S::Expired:                 return "expired";
        case S::InGracePeriod:           return "expired, inside grace period";
        case S::HardwareMismatch:        return "issued for a different machine";
        case S::SignatureInvalid:        return "signature does not verify against this public key";
        case S::MalformedFile:           return "not a license file we can parse";
        case S::NotYetValid:             return "not yet valid";
        case S::FeatureNotLicensed:      return "that feature is not in this license";
        case S::NoLicensesAvailable:     return "no floating seats free";
        case S::ServerUnreachable:       return "floating license server unreachable";
        case S::LibraryNotInitialized:   return "library not initialized";
        case S::TierNotAuthorized:       return "feature requires a higher library tier";
        case S::GenerationLimitReached:  return "license generation limit reached";
        case S::MachineNotAuthorized:    return "machine not an authorized developer seat";
        case S::ClockManipulated:        return "system clock appears to have been rolled back";
        case S::IntegrityCheckFailed:    return "the library binary has been modified";
        case S::MachineSeatLimitReached: return "this machine has reached its seat cap";
        case S::VersionMismatch:         return "this app version is outside the license version range";
        case S::KeyMalformed:            return "activation key is malformed";
        case S::KeyChecksumMismatch:     return "activation key checksum failed -- likely a typo";
    }
    return "unknown status";
}

static void report(const std::string& path,
                   rockyguard::LicenseVerifier& verifier,
                   const rockyguard::LicenseResult& loaded,
                   const rockyguard::LicenseResult& node,
                   bool licensed,
                   bool has3D,
                   bool hasStl) {
    std::printf("RockyGuard %s\n", rockyguard::VERSION_STRING);
    std::printf("license file : %s\n", path.c_str());
    std::printf("status       : %s\n", explain(loaded.status));

    // Never swallow this. A license that is present but rejected is a support
    // call waiting to happen, and the library already wrote you the sentence.
    if (!loaded.message.empty()) {
        std::printf("detail       : %s\n", loaded.message.c_str());
    }
    if (loaded.status == rockyguard::LicenseStatus::InGracePeriod) {
        std::printf("grace left   : %d day(s)\n", loaded.grace_days_remaining);
    }

    // Report the node-lock check SEPARATELY. load() can succeed on a licence that
    // is cryptographically perfect and simply belongs to another machine: it
    // verifies the signature and the dates, and knows nothing about hardware. Show
    // only loaded.status and such a licence prints "valid" and then silently
    // behaves as unlicensed, which is the least debuggable outcome possible and a
    // very common real support case.
    if (static_cast<bool>(loaded) && !static_cast<bool>(node)) {
        std::printf("node lock    : FAILED -- %s\n", explain(node.status));
        if (!node.message.empty()) {
            std::printf("               %s\n", node.message.c_str());
        }
    } else if (static_cast<bool>(node)) {
        std::printf("node lock    : matches this machine\n");
    }

    if (licensed) {
        // license() returns BY VALUE as of v1.3 -- `auto&` will not compile,
        // and that is deliberate. Copy it, or bind to a const reference.
        const rockyguard::License lic = verifier.license();
        std::printf("licensee     : %s\n", lic.licensee.c_str());
        std::printf("product      : %s\n", lic.product.c_str());
        std::printf("expires      : %s\n", lic.expires_at.c_str());
        std::printf("features     : ");
        for (const std::string& f : lic.features) {
            std::printf("%s ", f.c_str());
        }
        std::printf("\n");
    }

    std::printf("\ntier         : %s\n", has3D ? "Pro (2D + 3D)" : "Draft (2D only)");
    std::printf("cad_3d       : %s\n", has3D ? "unlocked" : "locked");
    std::printf("stl export   : %s\n", hasStl ? "unlocked" : "locked");
}

int main(int argc, char** argv) {
    const std::string path = (argc > 1) ? argv[1] : "license.lic";

    // The constructor THROWS if the PEM above will not parse. Verified against
    // v1.3.2, which raises std::runtime_error("Failed to parse public key PEM:
    // error:1E08010C:DECODER routines::unsupported").
    //
    // Wrap it. This is the one piece the five-line snippet in the docs leaves
    // out, and leaving it out is expensive: an uncaught throw here terminates
    // the process with 0xC0000409 and prints NOTHING AT ALL, so the first thing
    // a developer sees after pasting a truncated key is a silent crash instead
    // of the perfectly good message the library wrote for them.
    //
    // A key that will not parse is a build-time mistake by YOU, not a licensing
    // state, so it earns a different message and a different exit code from
    // "unlicensed".
    try {
        // ---- the integration: five lines ---------------------------------
        rockyguard::LicenseVerifier verifier(kPublicKey);
        const rockyguard::LicenseResult loaded = verifier.load(path);
        // Keep the node-lock RESULT, not just a bool, so its reason can be shown.
        const rockyguard::LicenseResult node =
            static_cast<bool>(loaded) ? verifier.check_node_locked() : loaded;
        const bool licensed = static_cast<bool>(node);
        const bool has3D = licensed && static_cast<bool>(verifier.check_feature(kFeature3D));
        const bool hasStl = licensed && static_cast<bool>(verifier.check_feature(kFeatureStl));
        // ---- end of integration ------------------------------------------

        report(path, verifier, loaded, node, licensed, has3D, hasStl);
    } catch (const std::exception& e) {
        std::fprintf(stderr,
                     "RockyGuard could not use the public key compiled into this binary.\n"
                     "  %s\n"
                     "This is a build configuration error, not a license problem. Check that\n"
                     "kPublicKey holds the complete PEM emitted by license_keygen, including\n"
                     "both the BEGIN and END PUBLIC KEY lines.\n",
                     e.what());
        return 2;
    }

    // Exit 0 even when unlicensed. This is a tiering demo, not a lockout demo:
    // the Draft tier is a product, not an error. Return non-zero only if you
    // really do intend the application to refuse to run at all.
    return 0;
}
