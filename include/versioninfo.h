#ifndef MXM_VERSION_INFO_H
#define MXM_VERSION_INFO_H

#include "MxmCommonMacros.h"
#include "mxmconfig.h"

#if defined(__clang__)
constexpr const char* MXM_BUILDCONF_COMPILER_VERSION = "Clang " __clang_version__;
#elif defined(__GNUC__)
constexpr const char* MXM_BUILDCONF_COMPILER_VERSION = "GCC " __VERSION__;
#elif defined(_MSC_VER)
constexpr const char* MXM_BUILDCONF_COMPILER_VERSION = "MSVC " MXM_STRINGIFY(_MSC_FULL_VER);
#else
constexpr const char* MXM_BUILDCONF_COMPILER_VERSION = "unknown compiler";
#endif

#if defined(MXM_HOST_X86)
constexpr const char* MXM_BUILDCONF_MACHINE = "i386";
#elif defined(MXM_HOST_X86_64)
constexpr const char* MXM_BUILDCONF_MACHINE = "x86_64";
#elif defined(MXM_HOST_ARM32)
constexpr const char* MXM_BUILDCONF_MACHINE = "arm32";
#elif defined(MXM_HOST_ARM64)
constexpr const char* MXM_BUILDCONF_MACHINE = "arm64";
#elif defined(MXM_HOST_RISCV32)
constexpr const char* MXM_BUILDCONF_MACHINE = "riscv32";
#elif defined(MXM_HOST_RISCV64)
constexpr const char* MXM_BUILDCONF_MACHINE = "riscv64";
#elif defined(MXM_HOST_PPC32)
constexpr const char* MXM_BUILDCONF_MACHINE = "ppc";
#elif defined(MXM_HOST_PPC64)
constexpr const char* MXM_BUILDCONF_MACHINE = "ppc64";
#else
constexpr const char* MXM_BUILDCONF_MACHINE = "unknown processor";
#endif

#if defined(MXM_BUILD_LINUX)
constexpr const char* MXM_BUILDCONF_PLATFORM = "Linux";
#elif defined(MXM_BUILD_APPLE)
constexpr const char* MXM_BUILDCONF_PLATFORM = "OS X";
#elif defined(MXM_BUILD_OPENBSD)
constexpr const char* MXM_BUILDCONF_PLATFORM = "OpenBSD";
#elif defined(MXM_BUILD_FREEBSD)
constexpr const char* MXM_BUILDCONF_PLATFORM = "FreeBSD";
#elif defined(MXM_BUILD_WIN32)
constexpr const char* MXM_BUILDCONF_PLATFORM = "win32";
#elif defined(MXM_BUILD_HAIKU)
constexpr const char* MXM_BUILDCONF_PLATFORM = "Haiku";
#elif defined(MXM_BUILD_CYGWIN)
constexpr const char* MXM_BUILDCONF_PLATFORM = "Cygwin";
#else
constexpr const char* MXM_BUILDCONF_PLATFORM = "unknown platform";
#endif

#endif // MXM_VERSION_INFO_H
