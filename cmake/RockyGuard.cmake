# RockyGuard.cmake -- locate the SDK, or fall back to the API stub.
#
# Defines one imported target: RockyGuard::RockyGuard
# Sets ROCKYGUARD_IS_STUB to TRUE when the real SDK was not found.
#
# Usage:
#   cmake -B build -DROCKYGUARD_ROOT=/path/to/rockyguard-v1.3.2-<platform>-customer
# Without ROCKYGUARD_ROOT the stub is used, everything still builds, and every
# license check reports "no license". That is what lets CI run on pull requests
# from forks, which cannot access repository secrets.

include_guard(GLOBAL)

set(ROCKYGUARD_ROOT "" CACHE PATH
    "Root of an unpacked RockyGuard customer SDK (the directory containing include/ and lib/)")

add_library(rockyguard_iface INTERFACE)
add_library(RockyGuard::RockyGuard ALIAS rockyguard_iface)

# ---------------------------------------------------------------------------
# Platform hardening. These apply in BOTH stub and SDK builds, deliberately:
# a contributor without the SDK must hit the same compiler behaviour as CI, or
# the stub build stops being a useful gate.
# ---------------------------------------------------------------------------
if(MSVC)
    # Without /utf-8 MSVC reads UTF-8 source as the system ANSI codepage. On a
    # GBK- or Shift-JIS-locale Windows box that mis-encodes any non-ASCII string
    # literal, or emits C4819. Our sources are ASCII-only as a second line of
    # defence, but a contributor's editor will not be.
    target_compile_options(rockyguard_iface INTERFACE /utf-8)
endif()

if(WIN32)
    # windows.h arrives transitively through the SDK's COM dependencies. Its
    # min/max macros break every std::min, std::max and
    # std::numeric_limits<T>::max() in the geometry code, with errors that do
    # not mention windows.h.
    target_compile_definitions(rockyguard_iface INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---------------------------------------------------------------------------
# Stub path
# ---------------------------------------------------------------------------
if(NOT ROCKYGUARD_ROOT)
    set(ROCKYGUARD_IS_STUB TRUE CACHE INTERNAL "" FORCE)
    target_include_directories(rockyguard_iface INTERFACE "${CMAKE_CURRENT_LIST_DIR}/../stub")
    message(STATUS "RockyGuard: using API STUB (no ROCKYGUARD_ROOT given).")
    message(STATUS "            Everything builds; all license checks report 'no license'.")
    message(STATUS "            For the licensed behaviour: -DROCKYGUARD_ROOT=/path/to/sdk")
    return()
endif()

# ---------------------------------------------------------------------------
# Real SDK path
# ---------------------------------------------------------------------------
set(ROCKYGUARD_IS_STUB FALSE CACHE INTERNAL "" FORCE)

if(NOT EXISTS "${ROCKYGUARD_ROOT}/include/rockyguard/rockyguard.h")
    message(FATAL_ERROR
        "ROCKYGUARD_ROOT is set to '${ROCKYGUARD_ROOT}' but "
        "include/rockyguard/rockyguard.h is not there.\n"
        "Point it at the unpacked customer bundle, e.g. "
        "rockyguard-v1.3.2-windows-x64-customer/")
endif()

target_include_directories(rockyguard_iface INTERFACE
    "${ROCKYGUARD_ROOT}/include"
    "${ROCKYGUARD_ROOT}/deps/include")

if(WIN32)
    # Two static libraries ship, one per CRT. Verified from the v1.3.2 bundle:
    #   lib/static/rockyguard.lib      -> /DEFAULTLIB:MSVCRT  (Release, /MD)
    #   lib/static/rockyguard_mdd.lib  -> /DEFAULTLIB:MSVCRTD (Debug,   /MDd)
    # Picking the wrong one is a wall of LNK2038 mismatch errors, so let the
    # generator expression choose per-configuration rather than per-build-tree.
    # (The bundled OpenSSL statics embed no /DEFAULTLIB directives at all, so
    # the same pair links into either configuration.)
    set(_rg_lib_dir "${ROCKYGUARD_ROOT}/lib/static")
    target_link_libraries(rockyguard_iface INTERFACE
        "$<IF:$<CONFIG:Debug>,${_rg_lib_dir}/rockyguard_mdd.lib,${_rg_lib_dir}/rockyguard.lib>"
        "${ROCKYGUARD_ROOT}/deps/lib/libssl.lib"
        "${ROCKYGUARD_ROOT}/deps/lib/libcrypto.lib"
        # wbemuuid and the ole32/oleaut32 pair are for the WMI queries behind
        # the Windows hardware fingerprint; iphlpapi for the MAC component.
        ws2_32 crypt32 iphlpapi ole32 oleaut32 wbemuuid)
else()
    target_link_libraries(rockyguard_iface INTERFACE
        "${ROCKYGUARD_ROOT}/lib/static/librockyguard.a"
        "${ROCKYGUARD_ROOT}/deps/lib/libssl.a"
        "${ROCKYGUARD_ROOT}/deps/lib/libcrypto.a"
        pthread dl)
endif()

message(STATUS "RockyGuard: using SDK at ${ROCKYGUARD_ROOT}")
