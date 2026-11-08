#include <gtest/gtest.h>

#include <core/config/EngineConfig.h>

#include "support/ScopedEnv.h"

TEST(EngineEnvironmentOverrides, ScratchAndAdapterReachAnyHostsConfig)
{
    ScopedEnv scratch("SENCHA_FRAME_SCRATCH_BYTES", "4096");
    ScopedEnv device("SENCHA_DEVICE_INDEX", "2");
    ScopedEnv dispatches("SENCHA_SKINNED_DISPATCHES", "7");
    EngineConfig config;

    ApplyEnvironmentOverrides(config);

    EXPECT_EQ(config.Graphics.FrameScratchBytesPerFrame, 4096u);
    EXPECT_EQ(config.Graphics.DeviceIndex, 2);
    EXPECT_EQ(config.Graphics.SkinnedPoseDispatchesPerFrame, 7u);
}

TEST(EngineEnvironmentOverrides, WindowSizeAndValidationSwitches)
{
    ScopedEnv size("SENCHA_WINDOW_SIZE", "1920x1080");
    ScopedEnv validation("SENCHA_VALIDATION", "0");
    ScopedEnv sync("SENCHA_VALIDATE_SYNC", "1");
    EngineConfig config;

    ApplyEnvironmentOverrides(config);

    EXPECT_EQ(config.Window.Width, 1920u);
    EXPECT_EQ(config.Window.Height, 1080u);
    EXPECT_FALSE(config.Graphics.EnableValidation);
    EXPECT_TRUE(config.Graphics.ValidateSynchronization);
}

TEST(EngineEnvironmentOverrides, UnsetOrMalformedValuesLeaveTheConfigAlone)
{
    ScopedEnv size("SENCHA_WINDOW_SIZE", "wide");
    ScopedEnv scratch("SENCHA_FRAME_SCRATCH_BYTES", "0");
    ScopedEnv device("SENCHA_DEVICE_INDEX", nullptr);
    EngineConfig config;
    const EngineConfig defaults;

    ApplyEnvironmentOverrides(config);

    EXPECT_EQ(config.Window.Width, defaults.Window.Width);
    EXPECT_EQ(config.Graphics.FrameScratchBytesPerFrame, defaults.Graphics.FrameScratchBytesPerFrame);
    EXPECT_EQ(config.Graphics.DeviceIndex, defaults.Graphics.DeviceIndex);
}
