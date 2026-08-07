#include "CookSession.h"

#include "document/CookGraph.h"
#include "document/EditorDocument.h"
#include "document/WorldCook.h"
#include "document/WorldDocument.h"
#include "project/Project.h"

#include <app/Engine.h>
#include <core/console/ConsoleService.h>
#include <core/console/ConsoleTypes.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <zone/WorldPartitionIds.h>

#include <algorithm>
#include <optional>
#include <span>
#include <variant>

struct CookSession::SharedState
{
    bool Active = false;
    bool Cancelling = false;
    std::string ProfileName;
    std::vector<std::string> OrderedSteps;
    std::string LastError;
    std::shared_ptr<CookControl> Control;
    Record Last;
    std::string LastWorld;
    std::string LastZone;
    std::uint64_t PublicationSerial = 0;
};

namespace
{
    double ReadDouble(Engine& engine, const char* name, double fallback)
    {
        const CVarMetadata* cvar = engine.Console().Registry().FindCVar(name);
        if (cvar != nullptr && std::holds_alternative<double>(cvar->CurrentValue))
            return std::get<double>(cvar->CurrentValue);
        return fallback;
    }

    LightingCookParams ReadLightingParams(Engine& engine)
    {
        LightingCookParams params;
        params.Shading.DiffuseWrap = static_cast<float>(
            ReadDouble(engine, "render.style.diffuse_wrap", params.Shading.DiffuseWrap));
        params.LuxelSize = static_cast<float>(
            ReadDouble(engine, "editor.cook.lightmap_luxel", params.LuxelSize));
        params.MaxAtlasSize = static_cast<std::uint32_t>(
            ReadDouble(engine, "editor.cook.lightmap_max_size", params.MaxAtlasSize));
        params.ConeDegrees = static_cast<float>(
            ReadDouble(engine, "editor.cook.lightmap_cone", params.ConeDegrees));
        params.Probe.BounceAlbedo = static_cast<float>(
            ReadDouble(engine, "editor.cook.probe_albedo", params.Probe.BounceAlbedo));
        params.Probe.SkyColor = Vec3d(
            static_cast<float>(ReadDouble(engine, "render.ambient.sky_r", 0.10)),
            static_cast<float>(ReadDouble(engine, "render.ambient.sky_g", 0.12)),
            static_cast<float>(ReadDouble(engine, "render.ambient.sky_b", 0.15)));
        params.Probe.GroundColor = Vec3d(
            static_cast<float>(ReadDouble(engine, "render.ambient.ground_r", 0.04)),
            static_cast<float>(ReadDouble(engine, "render.ambient.ground_g", 0.03)),
            static_cast<float>(ReadDouble(engine, "render.ambient.ground_b", 0.02)));
        params.Ao.Enabled = ReadDouble(
            engine, "editor.cook.ao_enabled", params.Ao.Enabled ? 1.0 : 0.0) != 0.0;
        params.Ao.MaxDistance = static_cast<float>(
            ReadDouble(engine, "editor.cook.ao_radius", params.Ao.MaxDistance));
        params.Ao.RayCount = static_cast<std::uint32_t>(
            ReadDouble(engine, "editor.cook.ao_rays", params.Ao.RayCount));
        return params;
    }
}

CookSession::CookSession(Engine& engine, WorldDocument& world,
                         ProjectDescriptor* project, RuntimeAssets* assets)
    : Engine_(engine)
    , World_(world)
    , Project_(project)
    , Assets_(assets)
    , State(std::make_shared<SharedState>())
{
}

CookSession::~CookSession()
{
    Cancel();
}

bool CookSession::Start(const CookProfile& profile,
                        bool forceRebuild,
                        std::string levelName)
{
    Logger& log = Engine_.Logging().GetLogger<CookSession>();
    if (State->Active)
    {
        log.Warn("cook: another cook is already active");
        return false;
    }
    if (Project_ == nullptr || Assets_ == nullptr)
    {
        State->LastError = "no project or asset system is open";
        log.Error("cook: {}", State->LastError);
        return false;
    }

    const ResolvedCookGraph graph = ResolveDocumentCookGraph(profile);
    if (!graph.Valid)
    {
        State->LastError = graph.Error;
        log.Error("cook: invalid profile '{}': {}", profile.Name, graph.Error);
        return false;
    }

    const double cellSize = ReadDouble(Engine_, "editor.cook.cell_size", 16.0);
    const LightingCookParams lighting = ReadLightingParams(Engine_);
    const std::filesystem::path assetsRoot =
        std::filesystem::path(Project_->Directory) / "assets";
    const DocumentCookOptions options{
        .Profile = &profile,
        .ForceRebuild = forceRebuild,
        .Control = std::make_shared<CookControl>(),
    };

    if (World_.IsWorld())
    {
        if (World_.IsDirty())
        {
            if (!World_.HasSaveTarget() || !World_.SaveWorld())
            {
                State->LastError = "save the world before cooking";
                log.Error("cook: {}", State->LastError);
                return false;
            }
        }

        std::string inputError;
        std::optional<WorldCookInput> input = CollectWorldCookInput(
            World_, assetsRoot, cellSize, Engine_.Logging(), Assets_, lighting,
            options, &inputError);
        if (!input.has_value())
        {
            State->LastError = inputError;
            log.Error("cook: {}", inputError);
            return false;
        }

        State->Active = true;
        State->Cancelling = false;
        State->ProfileName = profile.Name;
        State->OrderedSteps = graph.OrderedSteps;
        State->LastError.clear();
        State->Control = options.Control;
        const std::string worldName = std::filesystem::path(
            std::string(World_.WorldPath())).stem().string();
        const std::string zoneName = ZoneIdToString(World_.FocusZone());
        auto inputBox = std::make_shared<std::optional<WorldCookInput>>(
            std::move(*input));
        const std::shared_ptr<SharedState> state = State;
        Task = Engine_.Tasks().Submit<WorldCookResult>(
            [inputBox, assetsRoot]() mutable
            {
                LoggingProvider workerLogging;
                WorldCookInput value = std::move(inputBox->value());
                inputBox->reset();
                return ExecuteWorldCook(
                    std::move(value), assetsRoot, workerLogging);
            },
            [state, worldName, zoneName](WorldCookResult result)
            {
                state->Active = false;
                state->Cancelling = false;
                if (!result.Success)
                {
                    state->LastError = result.Error;
                    return;
                }
                state->LastError.clear();
                state->LastWorld = worldName;
                state->LastZone = zoneName;
                ++state->PublicationSerial;
            });
        return true;
    }

    // Same rule as world mode above: the cook must be a function of what is on
    // disk, so unsaved authoring edits reach the file before they reach a
    // cooked artifact.
    if (EditorDocument& focus = World_.FocusDocument(); focus.IsDirty())
    {
        if (!focus.HasFilePath() || !focus.Save())
        {
            State->LastError = "save the level before cooking";
            log.Error("cook: {}", State->LastError);
            return false;
        }
    }

    if (levelName.empty())
    {
        const EditorDocument& document = World_.FocusDocument();
        levelName = document.HasFilePath()
            ? std::filesystem::path(document.GetDisplayName()).stem().string()
            : "untitled";
    }

    std::string inputError;
    std::optional<DocumentCookInput> input = CollectDocumentCookInput(
        World_.FocusDocument(), assetsRoot, cellSize, Engine_.Logging(), Assets_,
        lighting, {}, options, &inputError);
    if (!input.has_value())
    {
        State->LastError = inputError;
        log.Error("cook: {}", inputError);
        return false;
    }

    State->Active = true;
    State->Cancelling = false;
    State->ProfileName = profile.Name;
    State->OrderedSteps = graph.OrderedSteps;
    State->LastError.clear();
    State->Control = options.Control;

    const std::string sourceRel = "levels/" + levelName + ".level.json";
    auto inputBox = std::make_shared<std::optional<DocumentCookInput>>(
        std::move(*input));
    const std::shared_ptr<SharedState> state = State;
    Task = Engine_.Tasks().Submit<DocumentCookResult>(
        [inputBox, levelName, sourceRel, assetsRoot]() mutable
        {
            LoggingProvider workerLogging;
            DocumentCookInput value = std::move(inputBox->value());
            inputBox->reset();
            return ExecuteDocumentCook(std::move(value), levelName, sourceRel,
                                       assetsRoot, workerLogging);
        },
        [state, levelName](DocumentCookResult result)
        {
            state->Active = false;
            state->Cancelling = false;
            if (!result.Success)
            {
                state->LastError = result.Error;
                return;
            }
            state->LastError.clear();
            state->LastWorld.clear();
            state->LastZone.clear();
            state->Last = Record{
                .CookedScenePath = result.CookedScenePath,
                .ContentHash = result.ContentHash,
                .Serial = state->Last.Serial + 1,
                .ProbeVolumeCount = static_cast<std::uint32_t>(result.ProbeVolumeCount),
                .ProbeCount = static_cast<std::uint32_t>(result.ProbeCount),
                .Map = "levels/" + levelName,
                .ProfileId = result.ProfileId,
                .ReusedSteps = std::move(result.ReusedSteps),
                .CacheHit = result.CacheHit,
            };
            ++state->PublicationSerial;
        });
    return true;
}

bool CookSession::StartById(std::string_view profileId,
                            bool forceRebuild,
                            std::string levelName)
{
    std::vector<CookProfile> profiles = AvailableProfiles();
    const auto found = std::find_if(
        profiles.begin(), profiles.end(),
        [profileId](const CookProfile& profile)
        {
            return profile.Id == profileId || profile.Name == profileId;
        });
    if (found == profiles.end())
    {
        State->LastError = "unknown cook profile '" + std::string(profileId) + "'";
        Engine_.Logging().GetLogger<CookSession>().Error("cook: {}", State->LastError);
        return false;
    }
    return Start(*found, forceRebuild, std::move(levelName));
}

std::vector<CookProfile> CookSession::AvailableProfiles() const
{
    if (Project_ == nullptr)
        return ResolveCookProfiles({});
    return ResolveCookProfiles(Project_->CookProfiles);
}

void CookSession::Cancel()
{
    if (!State->Active)
        return;
    State->Cancelling = true;
    if (State->Control != nullptr)
        State->Control->RequestCancel();
    if (Task.IsValid() && Engine_.Tasks().Cancel(Task))
    {
        State->Active = false;
        State->Cancelling = false;
        State->LastError = "cook cancelled";
    }
}

bool CookSession::IsActive() const
{
    return State->Active;
}

CookSession::Status CookSession::GetStatus() const
{
    Status status;
    status.Active = State->Active;
    status.Cancelling = State->Cancelling;
    status.ProfileName = State->ProfileName;
    status.LastError = State->LastError;
    if (State->Control != nullptr)
    {
        status.CompletedSteps = State->Control->CompletedSteps.load();
        status.TotalSteps = State->Control->TotalSteps.load();
        const std::uint32_t current = State->Control->CurrentStep.load();
        if (current < State->OrderedSteps.size())
            status.StepName = State->OrderedSteps[current];
    }
    return status;
}

const CookSession::Record* CookSession::LastRecord() const
{
    return State->Last.Serial != 0 ? &State->Last : nullptr;
}

const std::string& CookSession::LastCookedWorld() const
{
    return State->LastWorld;
}

const std::string& CookSession::LastCookedZone() const
{
    return State->LastZone;
}

std::uint64_t CookSession::PublicationSerial() const
{
    return State->PublicationSerial;
}

void CookSession::RegisterCommands(ConsoleRegistry& registry)
{
    registry.RegisterCVar({
        .Name = "editor.cook.cell_size",
        .Owner = "editor",
        .Type = CVarType::Double,
        .DefaultValue = 16.0,
        .CurrentValue = 16.0,
        .Flags = CVarFlags::Archive,
        .Help = "World-space grid size used to cluster level geometry into cells.",
        .Source = { "editor" },
        .Min = 0.0,
    });

    const auto registerBakeDouble = [&registry](const char* name, double defaultValue,
                                                const char* help, double min, double max)
    {
        registry.RegisterCVar({
            .Name = name,
            .Owner = "editor",
            .Type = CVarType::Double,
            .DefaultValue = defaultValue,
            .CurrentValue = defaultValue,
            .Flags = CVarFlags::Archive,
            .Help = help,
            .Source = { "editor" },
            .Min = min,
            .Max = max,
        });
    };
    registerBakeDouble("editor.cook.lightmap_luxel", 0.25,
                       "World units per lightmap texel. Smaller values are finer.",
                       0.05, 4.0);
    registerBakeDouble("editor.cook.lightmap_max_size", 2048.0,
                       "Maximum lightmap atlas dimension.", 128.0, 4096.0);
    registerBakeDouble("editor.cook.lightmap_cone", 45.0,
                       "Lightmap chart normal-cone split limit in degrees.", 5.0, 90.0);
    registerBakeDouble("editor.cook.probe_albedo", 0.35,
                       "Surface reflectance used by the irradiance-probe bounce.", 0.0, 1.0);
    registerBakeDouble("editor.cook.ao_enabled", 1.0,
                       "Whether ambient occlusion is included in lighting cooks.", 0.0, 1.0);
    registerBakeDouble("editor.cook.ao_radius", 1.0,
                       "Ambient-occlusion contact reach in world units.", 0.1, 8.0);
    registerBakeDouble("editor.cook.ao_rays", 64.0,
                       "Ambient-occlusion hemisphere samples per texel.", 8.0, 512.0);

    ConsoleCommandMetadata cook;
    cook.Name = "cook";
    cook.Owner = "editor";
    cook.Usage = "cook [--rebuild] [profile-id] [level-name]";
    cook.Help = "Start an incremental cook with a named profile. The profile defaults to full.";
    cook.Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args)
    {
        ConsoleResult result;
        bool rebuild = false;
        std::size_t index = 0;
        if (index < args.size() && args[index] == "--rebuild")
        {
            rebuild = true;
            ++index;
        }
        if (index < args.size() && args[index] == "cancel")
        {
            Cancel();
            result.Info("cook cancellation requested");
            return result;
        }
        std::string profile = "full";
        std::string name;
        if (index < args.size())
        {
            const std::string& candidate = args[index];
            const std::vector<CookProfile> profiles = AvailableProfiles();
            const bool isProfile = std::ranges::any_of(
                profiles, [&candidate](const CookProfile& value)
                { return value.Id == candidate || value.Name == candidate; });
            if (isProfile)
            {
                profile = candidate;
                ++index;
            }
            else
            {
                name = candidate;
                ++index;
            }
        }
        if (index < args.size())
            name = args[index];
        if (!StartById(profile, rebuild, name))
            result.Error(State->LastError.empty() ? "cook failed" : State->LastError);
        else
            result.Info("started " + profile + " cook");
        return result;
    };
    registry.RegisterCommand(std::move(cook));
}
