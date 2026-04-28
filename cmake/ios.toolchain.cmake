# ---------------------------------------------------------------------------
# Sama Engine — Minimal iOS toolchain file.
#
# Targets the iOS Simulator by default (arm64 + x86_64 fat slice).  Pass
# -DSAMA_IOS_PLATFORM=OS to target a real device (arm64 only).
#
# Usage:
#   cmake -S . -B build/ios-sim \
#         -G Xcode \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/ios.toolchain.cmake \
#         -DSAMA_IOS=ON
#
# Mirrors the Android toolchain pattern: this file just sets CMAKE_SYSTEM_NAME
# and the SDK / arch knobs; everything else (frameworks, app targets) lives in
# the top-level CMakeLists.txt under SAMA_IOS gates.
#
# Phase A scope: simulator only, no code-signing.  Real device deployment
# (codesigning, entitlements, IPA packaging) is Phase D.
# ---------------------------------------------------------------------------

set(CMAKE_SYSTEM_NAME iOS)

# Deployment target — iOS 15 covers >95% of active devices and gives us all
# Metal 2 features the engine relies on.  Override with -DSAMA_IOS_DEPLOYMENT
# at configure time if a different floor is needed.
if(NOT DEFINED SAMA_IOS_DEPLOYMENT)
    set(SAMA_IOS_DEPLOYMENT "15.0")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET ${SAMA_IOS_DEPLOYMENT}
    CACHE STRING "Minimum iOS deployment target" FORCE)

# Platform: SIMULATOR (default) or OS (real device).  Phase A only exercises
# the SIMULATOR path; OS is wired up so Phase D can flip the switch without
# rewriting the toolchain.
if(NOT DEFINED SAMA_IOS_PLATFORM)
    set(SAMA_IOS_PLATFORM "SIMULATOR")
endif()

if(SAMA_IOS_PLATFORM STREQUAL "SIMULATOR")
    set(CMAKE_OSX_SYSROOT iphonesimulator CACHE STRING "" FORCE)
    # arm64 (Apple Silicon Macs) + x86_64 (Intel Macs) — the universal
    # simulator slice that boots on every developer machine.
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
elseif(SAMA_IOS_PLATFORM STREQUAL "OS")
    set(CMAKE_OSX_SYSROOT iphoneos CACHE STRING "" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
else()
    message(FATAL_ERROR
        "SAMA_IOS_PLATFORM must be SIMULATOR or OS, got '${SAMA_IOS_PLATFORM}'")
endif()

# Flip on SAMA_IOS so the top-level CMakeLists.txt knows to register the
# iOS-specific targets.  The top-level option() default is OFF, so users who
# point at this toolchain file get the right behaviour without an extra flag.
set(SAMA_IOS ON CACHE BOOL "Build for iOS" FORCE)

# When using -G Xcode, leave codesigning entirely off for the simulator.
# This is the bit that lets the build run without any Apple Developer account.
# Phase D will replace this with proper team / entitlement plumbing.
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED      "NO" CACHE STRING "" FORCE)
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED     "NO" CACHE STRING "" FORCE)
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY        "" CACHE STRING "" FORCE)

# bgfx's internal Metal headers expect MACOSX_DEPLOYMENT_TARGET to be unset;
# Apple LLVM warns/errors when both are present.  Clear it to be safe.
unset(ENV{MACOSX_DEPLOYMENT_TARGET})
