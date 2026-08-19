# RockyGuard examples

Worked examples of license gating with [RockyGuard](https://rockyguard.dev), a
C++17 licensing SDK. Two of them, on purpose:

| | What it proves |
|---|---|
| **`examples/01-minimal`** | The integration really is about five lines. One file, ~140 lines including all the reporting, no GUI. |
| **`examples/02-qt-cad`** | It survives a real application. A Qt Widgets CAD tool where 2D drafting is free and 3D is a paid feature -- the way AutoCAD LT relates to AutoCAD. |

The five lines, verbatim from `01-minimal/main.cpp`:

```cpp
rockyguard::LicenseVerifier verifier(kPublicKey);
const rockyguard::LicenseResult loaded = verifier.load(path);
const bool licensed = static_cast<bool>(loaded) && static_cast<bool>(verifier.check_node_locked());
const bool has3D    = licensed && static_cast<bool>(verifier.check_feature("cad_3d"));
const bool hasStl   = licensed && static_cast<bool>(verifier.check_feature("cad_stl_export"));
```

## Build

The SDK is proprietary and is not in this repo. **Without it everything still
builds** against an API stub that always reports "no license", which is how CI
runs on pull requests from forks:

```bash
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

With the SDK, for the licensed behaviour:

```bash
cmake -B build -S . -DROCKYGUARD_ROOT=/path/to/rockyguard-v1.3.2-windows-x64-customer
cmake --build build --config Release
```

Prerequisites, stated precisely rather than optimistically -- this is two
commands plus a dependency, not one command:

- **Ubuntu 22.04+**: `sudo apt install qt6-base-dev cmake` (system Qt is 6.2.4;
  the build targets 6.2 so this works without the Qt installer)
- **Windows**: MSVC 2019+ and Qt 6, with
  `-DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64`

Both Release and Debug are supported on Windows. CMake selects
`rockyguard_mdd.lib` for Debug and `rockyguard.lib` for Release automatically --
mixing them produces a wall of LNK2038 errors.

## What license enforcement does and does not do

Worth being straight about, because this audience will test the claim.

RockyGuard **raises the cost and time** of cracking, stops casual key sharing and
license transfer between machines, protects revenue during the launch window when
it matters most, and verifies fully offline so air-gapped customers are not
punished for having no internet.

It does **not** make software uncrackable. Nothing does. Anyone claiming
otherwise is selling something. In an open-source example repo the point is
sharper still: you can delete the check and rebuild in ten seconds. That is fine
-- the gate here exists to show you *where* checks go and *what the states are*,
not to defend this demo.

So there is no defense-in-depth theatre in this code. One function, two call
sites, ~50 lines, all in `licensing/gate.cpp`. CI enforces that the SDK header
appears nowhere else, because "the integration is five lines" has to stay true.

## Two things we found building this

Both verified against v1.3.2 with MSVC 19.44, both worth knowing before you
integrate:

1. **`LicenseVerifier`'s constructor throws if the PEM will not parse** --
   `std::runtime_error("Failed to parse public key PEM: ...")`. The five-line
   snippet in the official docs does not wrap it, and an uncaught throw here
   terminates with exit code `0xC0000409` and prints **nothing at all**. So a
   developer who pastes a truncated key sees a silent crash instead of the
   perfectly good message the library wrote. `01-minimal` wraps it; you should
   too.

2. **In Debug that same bad key calls `abort()` instead of throwing** -- not
   catchable, and on Windows it opens a modal dialog. Press F5 with a mistyped key
   and you get a box no `catch` block can intercept. Reproduce it deliberately
   with `api_canary --probe-bad-key`; it is off by default because an unattended
   run would hang.

## Layout

```
examples/01-minimal/     the five-line claim, in one file
examples/02-qt-cad/      the same claim inside a real Qt application
stub/rockyguard/         API-shaped stub so the repo builds without the SDK
cmake/RockyGuard.cmake   SDK discovery, CRT selection, platform hardening
tests/api_canary.cpp     compile-time guard: every SDK symbol we depend on
scripts/check_ascii.py   sources stay ASCII (MSVC reads UTF-8 as ANSI without /utf-8)
THIRD-PARTY.md           licensing posture, verified against primary sources
```

## Licensing

Our code is MIT. Qt is LGPLv3 and is not redistributed here. The RockyGuard SDK
is proprietary and is not redistributed here. **This repo ships source only** --
no Releases, no build artifacts -- because LGPLv3 s.4 attaches when a combined
*binary* is conveyed. Read `THIRD-PARTY.md` before changing that.
