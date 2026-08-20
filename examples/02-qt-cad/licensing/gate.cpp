// The ONLY translation unit that may include <rockyguard/rockyguard.h>.
// CI enforces this (see .github/workflows/ci.yml). Everything else in the app
// talks to gate.h.

#include "gate.h"

#include <rockyguard/rockyguard.h>

#include <cstdio>   // fopen/fclose: distinguishing 'no license file' from
                   // 'unreadable license file', which the SDK reports with
                   // the same MalformedFile status.
#include <cstdlib>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>

namespace lic {
namespace {

// Your vendor public key, emitted by tools/license_keygen. It is a PUBLIC key --
// compiling it into the binary is the intended design. The private key never
// leaves your build machine.
const char* kPublicKey = R"(-----BEGIN PUBLIC KEY-----
MCowBQYDK2VwAyEAIicBg8+AY+6zuO8v5OwRXeSrQKlmMKopMYZxkAv3tVc=
-----END PUBLIC KEY-----
)";

// Overridable so a reader can try the three sample licenses without rebuilding:
//   RGCAD_LICENSE=examples/licenses/expired.lic ./rgcad
std::string resolvePath() {
    if (const char* env = std::getenv("RGCAD_LICENSE")) {
        if (*env) return env;
    }
    return "rgcad.lic";
}

bool fileExists(const std::string& path) {
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}

std::once_flag g_once;
Status g_status;
std::set<std::string> g_features;

// Map the SDK's 20 statuses onto our three. The mapping is the interesting part:
// only Valid and InGracePeriod are Valid, and everything else that is not "the
// file simply is not there" is a FAILURE the user must be told about.
// No `path` parameter: the filesystem probe moved to initialize(), which runs it
// unconditionally so that every downstream message can be accurate about the file
// even on paths that never open it. Result is in g_status.filePresent.
void classify(const rockyguard::LicenseResult& r) {
    using S = rockyguard::LicenseStatus;

    // In a stub build there is no licensing ANSWER, so there is nothing to
    // reject and nothing to confirm. Returning NoLicense here keeps the UI from
    // shouting "License rejected" over a perfectly good file -- but the UI must
    // also not claim the file is MISSING, which is what it did until
    // Status::filePresent existed. The stub never opens the file at all, so it
    // gets to say exactly that and nothing more.
    if (g_status.stub) {
        g_status.state = State::NoLicense;
        return;
    }

    g_status.message = r.message;
    g_status.graceDaysRemaining = r.grace_days_remaining;
    g_status.inGracePeriod = (r.status == S::InGracePeriod);

    if (r.status == S::Valid || r.status == S::InGracePeriod) {
        g_status.state = State::Valid;
        return;
    }

    // MalformedFile is the status for "could not open the file" as well as for
    // "the bytes are not a license", so distinguish them by whether a file is
    // actually there. Without this, a first run with no license reports an
    // alarming parse error instead of "you are on the Draft tier".
    if (r.status == S::MalformedFile) {
        if (g_status.filePresent) {
            g_status.state = State::Invalid;  // present but unreadable
        } else {
            g_status.state = State::NoLicense;
            g_status.message.clear();  // absence is not an error worth reporting
        }
        return;
    }

    g_status.state = State::Invalid;
}

void initialize() {
    const std::string path = resolvePath();

    // Probed unconditionally and BEFORE anything else, so every later message can
    // be accurate about the file regardless of which path we take. The stub build
    // needs this precisely because it never opens the file itself.
    g_status.filePresent = fileExists(path);

#ifdef ROCKYGUARD_STUB
    g_status.stub = true;
#endif

    // The constructor THROWS if the embedded PEM will not parse -- verified on
    // v1.3.2: std::runtime_error("Failed to parse public key PEM: ...").
    //
    // This must be wrapped. An uncaught throw here terminates the process with
    // exit code 0xC0000409 and prints NOTHING, and in a Debug build the SDK calls
    // abort() instead, which is a modal dialog on Windows and not catchable at
    // all. Either way the developer sees a crash rather than the perfectly good
    // message the library wrote.
    //
    // A key that will not parse is OUR build error, not a licensing state, so it
    // gets its own flag and its own message. Never show the user "unlicensed"
    // when the truth is "we shipped a broken key".
    try {
        rockyguard::LicenseVerifier verifier(kPublicKey);

        const rockyguard::LicenseResult loaded = verifier.load(path);
        classify(loaded);

        if (g_status.state == State::Valid) {
            // Node-lock is a separate check from load(). A license can verify
            // cryptographically and still belong to another machine.
            const rockyguard::LicenseResult node = verifier.check_node_locked();
            if (!static_cast<bool>(node)) {
                g_status.state = State::Invalid;
                g_status.message = node.message;
                // classify() may have set this from load(); a node-lock failure
                // supersedes it, or the badge reads
                // "(license rejected)  (grace)" at the same time.
                g_status.inGracePeriod = false;
                g_status.graceDaysRemaining = 0;
                return;
            }

            const rockyguard::License l = verifier.license();
            g_status.licensee = l.licensee;
            g_status.expires = l.expires_at;

            // Ask the verifier, not License::has_feature(). has_feature() reads a
            // copied struct and knows nothing about expiry or node-lock, so it
            // would report a feature present on an expired license.
            for (const char* f : {k3D, kStlExport}) {
                if (static_cast<bool>(verifier.check_feature(f))) {
                    g_features.insert(f);
                }
            }
        }
    } catch (const std::exception& e) {
        g_status.state = State::NoLicense;
        g_status.keyError = true;
        g_status.message = std::string("the public key compiled into this binary "
                                       "will not parse: ") + e.what();
    }
}

}  // namespace

const Status& status() {
    std::call_once(g_once, initialize);
    return g_status;
}

bool has(const char* feature) {
    status();  // ensure initialized
    return g_status.state == State::Valid && g_features.count(feature) != 0;
}

std::string licensePath() { return resolvePath(); }

}  // namespace lic
