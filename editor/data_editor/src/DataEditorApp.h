#pragma once

#include <app/Game.h>

#include <memory>
#include <optional>
#include <string>

class DataEditorServices;

class DataEditorApp final : public Game
{
public:
    DataEditorApp(std::optional<std::string> projectPath,
                  std::optional<std::string> initialAsset);
    ~DataEditorApp() override;

    void OnConfigure(GameConfigureContext& ctx) override;
    void OnStart(GameStartupContext& ctx) override;
    void OnRegisterSystems(SystemRegisterContext& ctx) override;
    void OnPlatformEvent(PlatformEventContext& ctx) override;
    void OnShutdown(GameShutdownContext& ctx) override;

private:
    std::optional<std::string> ProjectPath;
    std::optional<std::string> InitialAsset;
    std::unique_ptr<DataEditorServices> Services;
};
