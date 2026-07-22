#pragma once

#include "document/DocumentCook.h"

#include <jobs/AsyncTaskQueue.h>
#include <project/CookProfile.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class Engine;
class WorldDocument;
class ConsoleRegistry;
struct ProjectDescriptor;
struct RuntimeAssets;

// Owns the editor's one active cook transaction. Input collection happens on
// the owner thread; the immutable backend input runs through AsyncTaskQueue and
// publishes its result at the engine completion phase.
class CookSession
{
public:
    struct Record
    {
        std::filesystem::path CookedScenePath;
        std::uint64_t ContentHash = 0;
        std::uint64_t Serial = 0;
        std::uint32_t ProbeVolumeCount = 0;
        std::uint32_t ProbeCount = 0;
        std::string Map;
        std::string ProfileId;
        std::vector<std::string> ReusedSteps;
        bool CacheHit = false;
    };

    struct Status
    {
        bool Active = false;
        bool Cancelling = false;
        std::string ProfileName;
        std::string StepName;
        std::uint32_t CompletedSteps = 0;
        std::uint32_t TotalSteps = 0;
        std::string LastError;
    };

    CookSession(Engine& engine, WorldDocument& world,
                ProjectDescriptor* project, RuntimeAssets* assets);
    ~CookSession();

    [[nodiscard]] bool Start(const CookProfile& profile,
                             bool forceRebuild = false,
                             std::string levelName = {});
    [[nodiscard]] bool StartById(std::string_view profileId,
                                 bool forceRebuild = false,
                                 std::string levelName = {});
    void Cancel();

    [[nodiscard]] std::vector<CookProfile> AvailableProfiles() const;
    void RegisterCommands(ConsoleRegistry& registry);

    [[nodiscard]] bool IsActive() const;
    [[nodiscard]] Status GetStatus() const;
    [[nodiscard]] const Record* LastRecord() const;
    [[nodiscard]] const std::string& LastCookedWorld() const;
    [[nodiscard]] const std::string& LastCookedZone() const;
    [[nodiscard]] std::uint64_t PublicationSerial() const;

private:
    struct SharedState;

    Engine& Engine_;
    WorldDocument& World_;
    ProjectDescriptor* Project_ = nullptr;
    RuntimeAssets* Assets_ = nullptr;
    std::shared_ptr<SharedState> State;
    AsyncTaskHandle Task;
};
