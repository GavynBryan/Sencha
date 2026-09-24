// Fitness function: the cross-module ABI descriptor's binary layout is frozen.
// GameModuleAbi is read across the .so boundary field-by-field, so its layout is
// a hard contract. If you change it, you are changing the ABI: update these
// expectations AND bump SENCHA_GAME_ABI_VERSION (the header fingerprint will
// change too). The static_asserts fail at compile time; the TEST mirrors them in
// the suite. (docs/architecture/hardening-and-consolidation.md W6.)

#include <app/GameModuleAbi.h>  // GameModuleAbi
#include <graphics/RenderFeature.h>  // RenderFeatureServices, RenderFrame
#include <navigation/NavigationTypes.h>  // NavLocation, NavRegion

#include <gtest/gtest.h>

#include <cstddef>

// StructSize must stay first (offset 0): the loader reads it before trusting any
// other field, so a size mismatch is caught even when the rest has drifted.
static_assert(offsetof(GameModuleAbi, StructSize) == 0, "GameModuleAbi.StructSize must be first");
static_assert(offsetof(GameModuleAbi, AbiVersion) == 4, "GameModuleAbi layout changed");
static_assert(offsetof(GameModuleAbi, HeaderFingerprint) == 8, "GameModuleAbi layout changed");
static_assert(offsetof(GameModuleAbi, CompilerId) == 16, "GameModuleAbi layout changed");
static_assert(offsetof(GameModuleAbi, CompilerMajor) == 20, "GameModuleAbi layout changed");
static_assert(offsetof(GameModuleAbi, StdLibId) == 24, "GameModuleAbi layout changed");
static_assert(offsetof(GameModuleAbi, StdLibVersion) == 28, "GameModuleAbi layout changed");
static_assert(offsetof(GameModuleAbi, PointerBits) == 32, "GameModuleAbi layout changed");
static_assert(offsetof(GameModuleAbi, BuildConfig) == 36, "GameModuleAbi layout changed");
static_assert(sizeof(GameModuleAbi) == 40, "GameModuleAbi size changed — bump SENCHA_GAME_ABI_VERSION");
static_assert(std::is_standard_layout_v<GameModuleAbi>, "GameModuleAbi must stay standard-layout (C-ABI readable)");

// Navigation references a game holds by value between queries. Their layout is
// compiled into the module; moving a field is an ABI change.
static_assert(offsetof(NavLocation, Zone) == 0, "NavLocation layout changed");
static_assert(offsetof(NavLocation, Generation) == 8, "NavLocation layout changed");
static_assert(offsetof(NavLocation, Profile) == 12, "NavLocation layout changed");
static_assert(offsetof(NavLocation, Ref) == 16, "NavLocation layout changed");
static_assert(offsetof(NavLocation, Position) == 24, "NavLocation layout changed");
static_assert(sizeof(NavLocation) == 40, "NavLocation size changed");
static_assert(offsetof(NavRegion, Ref) == 16, "NavRegion layout changed");
static_assert(sizeof(NavRegion) == 24, "NavRegion size changed");
static_assert(std::is_trivially_copyable_v<NavLocation>, "NavLocation must stay a plain value");

TEST(ModuleAbi, DescriptorLayoutIsFrozen)
{
    EXPECT_EQ(sizeof(GameModuleAbi), 40u);
    EXPECT_EQ(offsetof(GameModuleAbi, StructSize), 0u);
    EXPECT_EQ(SenchaThisBuildAbi().StructSize, sizeof(GameModuleAbi));
}

// The feature contract's parameter structs are module-facing: a module's
// IRenderFeature receives them by reference and reads members at compiled-in
// offsets. The fingerprint (graphics/*.h) catches accidental drift and refuses
// the module at load; these freezes make a deliberate change loud at build
// time, where the version bump it requires is decided.
static_assert(sizeof(RenderFrame) == 48, "RenderFrame layout changed — bump SENCHA_GAME_ABI_VERSION");
static_assert(offsetof(RenderFrame, FrameInFlightIndex) == 0, "RenderFrame layout changed");
static_assert(offsetof(RenderFrame, TargetExtent) == 4, "RenderFrame layout changed");
static_assert(offsetof(RenderFrame, Phase) == 12, "RenderFrame layout changed");
static_assert(offsetof(RenderFrame, Retirement) == 16, "RenderFrame layout changed");
static_assert(offsetof(RenderFrame, Instrumentation) == 32, "RenderFrame layout changed");
static_assert(offsetof(RenderFrame, Backend) == 40, "RenderFrame layout changed");
static_assert(sizeof(RenderFeatureServices) == 48,
              "RenderFeatureServices layout changed — bump SENCHA_GAME_ABI_VERSION");
static_assert(offsetof(RenderFeatureServices, Logging) == 0, "RenderFeatureServices layout changed");
static_assert(offsetof(RenderFeatureServices, Instrumentation) == 8, "RenderFeatureServices layout changed");
static_assert(offsetof(RenderFeatureServices, Buffers) == 16, "RenderFeatureServices layout changed");
static_assert(offsetof(RenderFeatureServices, Images) == 24, "RenderFeatureServices layout changed");
static_assert(offsetof(RenderFeatureServices, Scratch) == 32, "RenderFeatureServices layout changed");
static_assert(offsetof(RenderFeatureServices, Backend) == 40, "RenderFeatureServices layout changed");

TEST(ModuleAbi, FeatureContractLayoutIsFrozen)
{
    EXPECT_EQ(sizeof(RenderFrame), 48u);
    EXPECT_EQ(sizeof(RenderFeatureServices), 48u);
}
