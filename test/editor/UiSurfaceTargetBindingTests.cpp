#include <gtest/gtest.h>

#include "render/UiSurfaceTargetRenderFeature.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/logging/LoggingProvider.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <memory>

//=============================================================================
// The host's half of the surface-target feature: what a binding is, and how
// long it lasts. Everything here runs without a device, because that is the
// point -- a composition root binds while it is wiring panels, long before any
// render feature has been set up.
//
// What the pixels look like is not testable here; that is the offscreen/window
// parity golden, which needs a host to spawn.
//=============================================================================

namespace
{
struct Fixture
{
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    std::unique_ptr<RuntimeAssets> Assets;
    std::unique_ptr<UiService> Ui;

    Fixture()
    {
        Assets = std::make_unique<RuntimeAssets>(Logging, Serializers, RuntimeAssets::ReferenceOnly{});
        Ui = std::make_unique<UiService>(Logging, Assets->Assets, Assets->UiPackages, Assets->Fonts, nullptr,
                                         nullptr);
    }
    ~Fixture() { Ui->Shutdown(); }
};
}

TEST(UiSurfaceTargetBinding, ABindingIsMadeWithoutADeviceAndShowsNothingUntilItHasDrawn)
{
    Fixture f;
    UiSurfaceTargetRenderFeature feature(*f.Ui, nullptr);
    const UiSurfaceId surface = f.Ui->CreateSurface("preview", RenderExtent{ 1920, 1080 });

    const UiSurfaceTargetId binding = feature.Bind(surface, Vec3d{ 0.0f, 0.0f, 0.0f });
    ASSERT_TRUE(binding.IsValid()) << "a host binds while it wires its panels, before Setup";
    EXPECT_EQ(feature.BindingCount(), 1u);
    EXPECT_EQ(feature.Display(binding), static_cast<ImTextureID>(0))
        << "nothing has been drawn into it yet";

    feature.Unbind(binding);
    EXPECT_EQ(feature.BindingCount(), 0u);
    EXPECT_EQ(feature.Display(binding), static_cast<ImTextureID>(0)) << "and a released binding stays gone";
}

TEST(UiSurfaceTargetBinding, ABindingOutlivesTheDeviceStateItNames)
{
    Fixture f;
    UiSurfaceTargetRenderFeature feature(*f.Ui, nullptr);
    const UiSurfaceId surface = f.Ui->CreateSurface("preview", RenderExtent{ 1920, 1080 });
    const UiSurfaceTargetId binding = feature.Bind(surface, Vec3d{ 0.0f, 0.0f, 0.0f });

    // Teardown releases the target, not the declaration: the panel holding this
    // id keeps it, and the next Setup makes the target again. A binding dropped
    // here would leave a host showing a hole with no way to notice.
    feature.Teardown();
    EXPECT_EQ(feature.BindingCount(), 1u);
    EXPECT_EQ(feature.Display(binding), static_cast<ImTextureID>(0));

    feature.Unbind(binding);
    EXPECT_EQ(feature.BindingCount(), 0u);
}
