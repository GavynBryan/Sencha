#pragma once

#include <app/GameModuleAbiFingerprint.h> // generated: SENCHA_GAME_ABI_FINGERPRINT
#include <app/ModuleExport.h>              // SENCHA_GAME_ABI_VERSION, SENCHA_GAME_EXPORT

#include <cstdint>

//=============================================================================
// GameModuleAbi — the build-identity + ABI fingerprint a game module reports so
// the loader can refuse an incompatible build with a precise message instead of
// crashing on a skewed vtable. (docs/plans/sencha-level-editor/09-module-abi-
// hardening.md, Tiers 0 + 1.)
//
// Read across the boundary through an `extern "C"` accessor — never a virtual —
// so validating the ABI never depends on the ABI being valid. POD with a frozen
// layout; StructSize is first so a size mismatch is caught before any other
// field is trusted.
//=============================================================================
struct GameModuleAbi
{
    std::uint32_t StructSize;        // sizeof(GameModuleAbi)
    std::uint32_t AbiVersion;        // SENCHA_GAME_ABI_VERSION at build
    std::uint64_t HeaderFingerprint; // hash of the module-facing ABI headers
    std::uint32_t CompilerId;        // 1 GCC, 2 Clang, 3 MSVC, 0 other
    std::uint32_t CompilerMajor;
    std::uint32_t StdLibId;          // 1 libstdc++, 2 libc++, 3 MSVC STL, 0 other
    std::uint32_t StdLibVersion;
    std::uint32_t PointerBits;       // sizeof(void*) * 8
    std::uint32_t BuildConfig;       // bit0 debug, bit1 ASan, bit3 TSan
};

namespace SenchaAbiDetail
{
    inline constexpr std::uint32_t CompilerId()
    {
#if defined(__clang__)
        return 2u;
#elif defined(__GNUC__)
        return 1u;
#elif defined(_MSC_VER)
        return 3u;
#else
        return 0u;
#endif
    }

    inline constexpr std::uint32_t CompilerMajor()
    {
#if defined(__clang__)
        return static_cast<std::uint32_t>(__clang_major__);
#elif defined(__GNUC__)
        return static_cast<std::uint32_t>(__GNUC__);
#elif defined(_MSC_VER)
        return static_cast<std::uint32_t>(_MSC_VER);
#else
        return 0u;
#endif
    }

    inline constexpr std::uint32_t StdLibId()
    {
#if defined(_LIBCPP_VERSION)
        return 2u;
#elif defined(__GLIBCXX__)
        return 1u;
#elif defined(_MSVC_STL_VERSION)
        return 3u;
#else
        return 0u;
#endif
    }

    inline constexpr std::uint32_t StdLibVersion()
    {
#if defined(_LIBCPP_VERSION)
        return static_cast<std::uint32_t>(_LIBCPP_VERSION);
#elif defined(_GLIBCXX_RELEASE)
        return static_cast<std::uint32_t>(_GLIBCXX_RELEASE);
#elif defined(_MSVC_STL_VERSION)
        return static_cast<std::uint32_t>(_MSVC_STL_VERSION);
#else
        return 0u;
#endif
    }

    inline constexpr std::uint32_t BuildConfig()
    {
        std::uint32_t flags = 0u;
#if !defined(NDEBUG)
        flags |= 1u;
#endif
#if defined(__SANITIZE_ADDRESS__)
        flags |= 2u;
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
        flags |= 2u;
#  endif
#endif
#if defined(__SANITIZE_THREAD__)
        flags |= 8u;
#elif defined(__has_feature)
#  if __has_feature(thread_sanitizer)
        flags |= 8u;
#  endif
#endif
        return flags;
    }
} // namespace SenchaAbiDetail

// The ABI record for the current translation unit's build — identical on the
// engine and a compatible module, divergent otherwise.
inline GameModuleAbi SenchaThisBuildAbi()
{
    return GameModuleAbi{
        .StructSize        = static_cast<std::uint32_t>(sizeof(GameModuleAbi)),
        .AbiVersion        = SENCHA_GAME_ABI_VERSION,
        .HeaderFingerprint = SENCHA_GAME_ABI_FINGERPRINT,
        .CompilerId        = SenchaAbiDetail::CompilerId(),
        .CompilerMajor     = SenchaAbiDetail::CompilerMajor(),
        .StdLibId          = SenchaAbiDetail::StdLibId(),
        .StdLibVersion     = SenchaAbiDetail::StdLibVersion(),
        .PointerBits       = static_cast<std::uint32_t>(sizeof(void*) * 8u),
        .BuildConfig       = SenchaAbiDetail::BuildConfig(),
    };
}

// A game module exports its ABI record via this macro, alongside its
// SenchaCreateGameModule factory. C linkage: the loader reads it with no vtable.
#define SENCHA_EXPORT_GAME_MODULE_ABI()                                      \
    extern "C" SENCHA_GAME_EXPORT const GameModuleAbi* SenchaGameModuleAbi()  \
    {                                                                         \
        static const GameModuleAbi senchaAbi = SenchaThisBuildAbi();          \
        return &senchaAbi;                                                    \
    }
