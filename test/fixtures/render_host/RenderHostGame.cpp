#include <anim/AnimationClipPlaybackSystem.h>
#include <app/Engine.h>
#include <app/GameContexts.h>
#include <app/Game.h>
#include <app/GameModule.h>
#include <app/RuntimeContent.h>
#include <assets/runtime/RuntimeAssets.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <render/feature/UiRenderFeature.h>
#include <ui/UiService.h>
#include <world/ComponentRegistrar.h>
#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <ecs/World.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldPartitionIds.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

//=============================================================================
// The render host: what the golden-image comparison and the renderer A/B bench
// run a cooked level in.
//
// It is a game only in the sense that something has to be, and it is the
// smallest one that can produce a picture: it puts a camera at a fixed pose and
// registers clip playback, because a posed skinned mesh is one of the things
// the goldens watch. Everything else -- mounting content, loading the map --
// is the engine's, so this does not have to be a second runtime host the way
// the scene viewer it replaces was.
//
// The camera pose is part of the golden contract. Moving it changes every
// reference image, so it is a constant here rather than anything configurable.
//=============================================================================
namespace
{
constexpr Vec3d kCameraPosition{ 0.0f, 3.0f, 10.0f };

// A deterministic orbit for the A/B bench, keyed to rendered frames rather than
// to time. A capture compares frame N of one build against frame N of another,
// which only means something if frame N is the same view in both; a time-based
// orbit would move with whatever frame rate each build happened to achieve.
// It is a capture tool, not gameplay, and it is off unless the bench asks.
struct ScriptedCameraPathSystem
{
    ScriptedCameraPathSystem(const EntityId& camera, const bool& enabled)
        : Camera(camera)
        , Enabled(enabled)
    {
    }

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        if (!Enabled)
            return;
        LocalTransform* transform = ctx.Entities.TryGet<LocalTransform>(Camera);
        if (transform == nullptr)
            return;

        const double angle = static_cast<double>(FrameCounter) * kAngularStep;
        transform->Value.Position = Vec3d{
            static_cast<float>(std::cos(angle) * kRadius), kHeight,
            static_cast<float>(std::sin(angle) * kRadius) };
        const float yaw = static_cast<float>(angle) + kPi; // face the orbit center
        transform->Value.Rotation =
            Quatf::FromAxisAngle(Vec3d::Up(), yaw)
            * Quatf::FromAxisAngle(Vec3d::Right(), kPitch);
        ++FrameCounter;
    }

    const EntityId& Camera;
    const bool& Enabled;
    std::uint64_t FrameCounter = 0;

    static constexpr double kAngularStep = 0.012;
    static constexpr double kRadius = 8.0;
    static constexpr double kHeight = 3.0;
    static constexpr float kPitch = -0.35f;
    static constexpr float kPi = 3.14159265358979323846f;
};

}  // namespace

// Authored UI, hosted the way an application hosts it rather than the way the
// engine might eventually offer it: this fixture builds its own UiService over
// the asset caches it already has, drives update and extraction from its own
// registered system, and stages the render feature itself. That is the second
// half of what the golden scene proves -- not just that authored UI draws, but
// that a plain Sencha application can put it on screen without engine privilege.
class UiHostSystem
{
public:
    UiHostSystem(Engine& engine, std::string packagePath)
        : EnginePtr(&engine)
        , PackagePath(std::move(packagePath))
    {
    }

    void FrameUpdate(FrameUpdateContext&)
    {
        if (!Initialised)
            Initialise();
        if (Ui != nullptr)
            Ui->Update();
    }

    void ExtractRender(RenderExtractContext&)
    {
        if (Ui != nullptr)
            Ui->ExtractRender();
    }

private:
    void Initialise()
    {
        // Once, and only once: a failed bring-up must not be retried every
        // frame, or the log becomes the failure.
        Initialised = true;

        if (PackagePath.empty())
            return;

        RuntimeAssets& assets = EnginePtr->Content().Assets();
        Ui = std::make_unique<UiService>(EnginePtr->Logging(), assets.Assets,
                                         assets.UiPackages, assets.Fonts,
                                         assets.Textures.get());
        if (!Ui->IsReady())
        {
            Ui.reset();
            return;
        }

        const VkExtent2D extent = EnginePtr->Graphics().Swapchain.GetExtent();
        Surface = Ui->CreateSurface("render_host", RenderExtent{ extent.width, extent.height });
        if (!Ui->OpenScreen(Surface, PackagePath).IsValid())
        {
            Ui.reset();
            return;
        }

        EnginePtr->Graphics().MainRenderer.AddFeature(
            std::make_unique<UiRenderFeature>(*Ui, assets.Textures.get()));
    }

    Engine* EnginePtr = nullptr;
    std::string PackagePath;
    std::unique_ptr<UiService> Ui;
    UiSurfaceId Surface;
    bool Initialised = false;
};

class RenderHostGame final : public Game
{
public:
    void OnRegisterComponents(ComponentRegistrar&) override {}

    void OnStart(GameStartupContext&) override
    {
        Engine& engine = GetEngine();
        World& world = engine.World().Entities();

        Transform3f transform;
        transform.Position = kCameraPosition;

        Camera = world.CreateEntity(PersistentStoragePartition);
        world.AddComponent<LocalTransform>(Camera, LocalTransform{ transform });
        world.AddComponent<WorldTransform>(Camera, WorldTransform{ transform });
        world.AddComponent<CameraComponent>(Camera, CameraComponent{});
        world.GetResource<ActiveCameraService>().SetActive(Camera);

        engine.Console().Registry().RegisterCVar({
            .Name = "render_host.camera.scripted",
            .Owner = "render_host",
            .Type = CVarType::Bool,
            .DefaultValue = false,
            .CurrentValue = false,
            .Flags = CVarFlags::Transient,
            .Help = "Drive the camera along a fixed deterministic orbit, so a "
                    "run renders an identical view sequence.",
            .Source = { "render_host" },
            .OnChange = [this](const CVarChangeContext& ctx) {
                ScriptedCamera = std::get<bool>(ctx.NewValue);
            },
        });

        engine.Console().Registry().RegisterCVar({
            .Name = "render_host.ui",
            .Owner = "render_host",
            .Type = CVarType::String,
            .DefaultValue = std::string{},
            .CurrentValue = std::string{},
            .Flags = CVarFlags::Transient,
            .Help = "Authored UI package to open over the scene "
                    "(\"asset://ui/golden.rml\"). Empty draws no UI.",
            .Source = { "render_host" },
            .OnChange = [this](const CVarChangeContext& ctx) {
                UiPackagePath = std::get<std::string>(ctx.NewValue);
            },
        });
    }

    void OnRegisterSystems(SystemRegisterContext& ctx) override
    {
        ctx.Schedule.Register<ScriptedCameraPathSystem>(Camera, ScriptedCamera);
        ctx.Schedule.Register<UiHostSystem>(GetEngine(), UiPackagePath);
        // Clip playback: a posed skinned mesh is one of the things the goldens
        // watch, and nothing else in this host would advance it.
        RegisterAnimationSystems(ctx.Schedule);
    }

    void OnShutdown(GameShutdownContext&) override
    {
        World& world = GetEngine().World().Entities();
        world.GetResource<ActiveCameraService>().SetActive(EntityId{});
        if (Camera.IsValid() && world.IsAlive(Camera))
            world.DestroyEntity(Camera);
        Camera = EntityId{};
    }

private:
    EntityId Camera;
    bool ScriptedCamera = false;
    std::string UiPackagePath;
};

extern "C" SENCHA_GAME_EXPORT Game* SenchaCreateGameModule()
{
    static RenderHostGame instance;
    return &instance;
}

SENCHA_EXPORT_GAME_MODULE_ABI()
