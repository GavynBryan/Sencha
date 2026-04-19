#include <gtest/gtest.h>
#include <core/config/EngineConfig.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    std::filesystem::path WriteTempConfig(const std::string& text)
    {
        std::filesystem::path path =
            std::filesystem::temp_directory_path() / "sencha_engine_config_test.json";
        std::ofstream file(path, std::ios::binary);
        file << text;
        return path;
    }
}

TEST(EngineConfig, DefaultsDescribeLaunchableEngine)
{
    EngineConfig config;

    EXPECT_EQ(config.App.Name, "Sencha Application");
    EXPECT_EQ(config.Window.Title, "Sencha");
    EXPECT_EQ(config.Window.Width, 1280u);
    EXPECT_EQ(config.Window.Height, 720u);
    EXPECT_EQ(config.Window.GraphicsApi, WindowGraphicsApi::Vulkan);
    EXPECT_DOUBLE_EQ(config.Runtime.FixedTickRate, 60.0);
    EXPECT_DOUBLE_EQ(config.Runtime.TargetFps, 0.0);
    EXPECT_FALSE(config.Runtime.ExitOnEscape);
    EXPECT_FALSE(config.Runtime.TogglePauseOnF1);
    EXPECT_EQ(config.Graphics.FramesInFlight, 2u);
    EXPECT_TRUE(config.Debug.ConsoleLogging);
    EXPECT_FALSE(config.Debug.DebugUi);
}

TEST(EngineConfig, LoadsAppWindowRuntimeGraphicsDebugAndAudio)
{
    const std::filesystem::path path = WriteTempConfig(R"({
        "app": {
            "name": "Cube Demo"
        },
        "window": {
            "title": "Sencha Cube Demo",
            "width": 1600,
            "height": 900,
            "mode": "borderless_fullscreen",
            "graphics_api": "vulkan",
            "resizable": false,
            "visible": true
        },
        "runtime": {
            "fixed_tick_rate": 120,
            "target_fps": 240,
            "resize_settle_seconds": 0.25,
            "max_ticks_per_frame": 8,
            "exit_on_escape": true,
            "toggle_pause_on_f1": true
        },
        "graphics": {
            "frames_in_flight": 3,
            "enable_validation": false
        },
        "debug": {
            "console_logging": false,
            "debug_ui": true
        },
        "audio": {
            "buses": [
                { "name": "Sfx", "maxVoices": 8, "volume": 0.75 }
            ]
        }
    })");

    EngineConfigError error;
    std::optional<EngineConfig> loaded = LoadEngineConfig(path.string().c_str(), &error);

    ASSERT_TRUE(loaded) << error.Message;
    EXPECT_EQ(loaded->App.Name, "Cube Demo");
    EXPECT_EQ(loaded->Window.Title, "Sencha Cube Demo");
    EXPECT_EQ(loaded->Window.Width, 1600u);
    EXPECT_EQ(loaded->Window.Height, 900u);
    EXPECT_EQ(loaded->Window.Mode, WindowMode::BorderlessFullscreen);
    EXPECT_EQ(loaded->Window.GraphicsApi, WindowGraphicsApi::Vulkan);
    EXPECT_FALSE(loaded->Window.Resizable);
    EXPECT_TRUE(loaded->Window.Visible);
    EXPECT_DOUBLE_EQ(loaded->Runtime.FixedTickRate, 120.0);
    EXPECT_DOUBLE_EQ(loaded->Runtime.TargetFps, 240.0);
    EXPECT_DOUBLE_EQ(loaded->Runtime.ResizeSettleSeconds, 0.25);
    EXPECT_EQ(loaded->Runtime.MaxTicksPerFrame, 8u);
    EXPECT_TRUE(loaded->Runtime.ExitOnEscape);
    EXPECT_TRUE(loaded->Runtime.TogglePauseOnF1);
    EXPECT_EQ(loaded->Graphics.FramesInFlight, 3u);
    EXPECT_FALSE(loaded->Graphics.EnableValidation);
    EXPECT_FALSE(loaded->Debug.ConsoleLogging);
    EXPECT_TRUE(loaded->Debug.DebugUi);
    ASSERT_EQ(loaded->Audio.Buses.size(), 1u);
    EXPECT_EQ(loaded->Audio.Buses[0].Name, "Sfx");

    std::filesystem::remove(path);
}

TEST(EngineConfig, RejectsInvalidFixedTickRate)
{
    const std::filesystem::path path = WriteTempConfig(R"({
        "runtime": {
            "fixed_tick_rate": 0
        }
    })");

    EngineConfigError error;
    std::optional<EngineConfig> loaded = LoadEngineConfig(path.string().c_str(), &error);

    EXPECT_FALSE(loaded);
    EXPECT_NE(error.Message.find("fixedTickRate"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(EngineConfig, RejectsInvalidWindowGraphicsApi)
{
    const std::filesystem::path path = WriteTempConfig(R"({
        "window": {
            "graphics_api": "directx"
        }
    })");

    EngineConfigError error;
    std::optional<EngineConfig> loaded = LoadEngineConfig(path.string().c_str(), &error);

    EXPECT_FALSE(loaded);
    EXPECT_NE(error.Message.find("graphicsApi"), std::string::npos);

    std::filesystem::remove(path);
}
