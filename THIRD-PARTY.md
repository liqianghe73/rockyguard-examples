# Third-party components and licensing

This repository is published by a company that sells license compliance, so its
own compliance posture is part of the product. Every claim below was verified
against a primary source, and the verification is recorded so it can be rechecked
rather than trusted.

## What this repo contains and how it is distributed

**Source only.** No compiled binaries, no GitHub Releases, no CI artifact
uploads. That is a deliberate compliance decision, not an oversight: LGPLv3 s.4
attaches when you *convey the combined binary*. A source-only repository conveys
no Combined Work and no Qt code at all, so essentially no LGPL obligation
attaches. It would attach the moment a built `.exe` were published here.

**Do not add a Releases page or upload build artifacts to this repo** without
first working through LGPLv3 s.4(a)-(e).

## Our own code

| Component | License |
|---|---|
| Everything under `examples/`, `stub/`, `cmake/`, `scripts/`, `tests/` | MIT (see `LICENSE`) |

MIT rather than Apache-2.0: this is example code meant to be copied into
proprietary products, and MIT is the least friction for that.

## RockyGuard SDK

**Proprietary. Not redistributed here, in any form.** No headers, no libraries,
no license files. You supply your own copy and point the build at it:

```
cmake -B build -DROCKYGUARD_ROOT=/path/to/rockyguard-v1.3.2-<platform>-customer
```

Without it, the build uses `stub/rockyguard/rockyguard.h` -- our own MIT-licensed
API-shaped stub that always reports "no license". The stub is not RockyGuard and
contains none of its code.

## Qt 6

Used only by `examples/02-qt-cad`. **Not redistributed** -- you install Qt
yourself and CMake links against it.

Modules used, all of which offer LGPLv3:

| Module | Open-source license | Verified at |
|---|---|---|
| Qt Core, Qt Gui, Qt Widgets | LGPLv3 or GPLv2 | `doc.qt.io/qt-6/qtcore-index.html`, `qtgui-index.html`, `qtwidgets-index.html` |

Two rules follow, and both are enforced in `CMakeLists.txt`:

1. **Dynamic Qt only.** The build fails hard on a static `Qt6::Core`. Qt's own
   legal guidance warns that static linking can mean the application is no longer
   a "work that uses the Library" and becomes subject to the LGPL itself -- which
   for an application linking a proprietary SDK would be fatal.
2. **No GPL-3.0 Qt module, ever.** Verified GPL-3.0-only in the open-source
   offering, and therefore forbidden here:
   **Qt Quick 3D**, **Qt Quick 3D Physics**, **Qt Charts**, **Qt Data
   Visualization**, **Qt Graphs**. Source: the "Modules available under GNU
   General Public License v3" list at `doc.qt.io/qt-6/licensing.html`, plus each
   module's own index page.

   Note there is therefore **no LGPL charting or 3D scene-graph module in Qt at
   all**. Qt 3D *is* LGPLv3 and is the sole LGPL 3D scene graph, but it has been
   deprecated since Qt 6.8 and is maintained by KDAB rather than The Qt Company,
   so it is avoided here on maintenance grounds -- not licensing grounds.

## A precise note on GPL build tools

**`moc` is GPL-3.0**, and every Qt Widgets build runs it. So is `qsb` in Qt
Shader Tools (which this repo does not use).

This is fine, and the distinction matters: the GPL governs distribution of *the
tool*, not the output of running it. We invoke `moc` at build time and never
redistribute it.

It does mean one claim must never appear in this repo: **"zero GPL-3.0
exposure."** The accurate statement is **"no GPL-3.0 code is linked into the
resulting binary,"** which is true and defensible.

## OpenSSL

Ships inside the RockyGuard SDK bundle (`deps/`), is not redistributed here, and
its version and license should be read from the SDK bundle's own files rather
than restated here, where the statement would go stale.

## Verification

`scripts/check_ascii.py` runs in CI. The compliance greps described in
`.github/workflows/ci.yml` -- SDK headers confined to the gate translation unit,
no committed binaries -- are the mechanical half of this document. A document
nothing checks drifts.
