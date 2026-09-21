#pragma once

#include <app/Game.h>

#include <memory>
#include <optional>
#include <string>

class AnimationEditorHost;

class AnimationEditorApp final : public Game
{
public:
    AnimationEditorApp(std::optional<std::string> projectPath,
                       std::optional<std::string> meshPath,
                       std::optional<std::string> clipPath);
    ~AnimationEditorApp() override;
    void OnConfigure(GameConfigureContext& context) override;
    void OnStart(GameStartupContext& context) override;
    void OnRegisterSystems(SystemRegisterContext& context) override;
    void OnPlatformEvent(PlatformEventContext& context) override;
    void OnShutdown(GameShutdownContext& context) override;

private:
    std::optional<std::string> ProjectPath;
    std::optional<std::string> MeshPath;
    std::optional<std::string> ClipPath;
    std::unique_ptr<AnimationEditorHost> Host;
};
