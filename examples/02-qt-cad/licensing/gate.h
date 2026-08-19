// The entire licensing surface this application sees.
//
// Two functions and a struct. That is the whole thing, and keeping it that small
// is the point: the claim being demonstrated is that integrating RockyGuard takes
// about five lines, and a reader must be able to see the whole integration
// without navigating.
//
// gate.cpp is the ONLY translation unit permitted to include
// <rockyguard/rockyguard.h>. CI greps for violations -- see .github/workflows/ci.yml.
// Nothing else in the app knows the SDK exists.
//
// Deliberately absent, and it is worth saying why: there are no capability
// tokens, no gate epochs, no atomics, no re-verification schedule, and no
// layered defense-in-depth. This is an open-source example -- a reader can delete
// the check and rebuild in ten seconds, so elaborate tamper resistance here would
// be theatre, and it would bury the very claim the repo exists to make. Where
// checks go and what the states are is the useful lesson.

#pragma once

#include <string>

namespace lic {

// Three states, not two. "No license" is a TIER (Draft), while "invalid" is a
// FAILURE that must be surfaced verbatim -- a rejected license is a support call
// waiting to happen, and the library already wrote the sentence explaining why.
// Collapsing them into a bool is how integrators end up swallowing diagnostics.
enum class State {
    NoLicense,  // nothing to load: the free Draft tier, a legitimate product
    Invalid,    // present but rejected: expired, wrong machine, bad signature
    Valid       // verified, including the in-grace-period case
};

struct Status {
    State state = State::NoLicense;
    std::string message;      // the library's own words, shown to the user as-is
    std::string licensee;     // empty unless Valid
    std::string expires;      // empty unless Valid
    int graceDaysRemaining = 0;
    bool inGracePeriod = false;
    bool stub = false;        // built against the API stub, not the SDK
    bool keyError = false;    // the embedded public key would not parse: OUR bug
};

// Feature names. Just strings in the license file, so they are yours to choose --
// but once shipped they are a compatibility surface, so keep them stable.
constexpr const char* k3D = "cad_3d";
constexpr const char* kStlExport = "cad_stl_export";

// Verified ONCE, on first call, and cached. Not because verification is slow to
// the point of mattering here, but because this application has no revocation
// story: there is nothing that could change the answer mid-session, so
// re-verifying would add moving parts and buy nothing.
const Status& status();

// The only question the rest of the app asks. False unless the license verified,
// so every failure path -- missing, expired, wrong machine, unparseable -- lands
// on the Draft tier rather than accidentally unlocking anything.
bool has(const char* feature);

// Path the license is loaded from, so the UI can tell the user where to put one.
std::string licensePath();

}  // namespace lic
