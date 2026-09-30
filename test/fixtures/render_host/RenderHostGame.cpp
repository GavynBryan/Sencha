#include <app/Engine.h>
#include <app/GameContexts.h>
#include <app/Game.h>
#include <app/GameModule.h>
#include <app/RuntimeContent.h>
#include <assets/runtime/RuntimeAssets.h>
#include <assets/texture/TextureCache.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/UiTextureResidency.h>
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

// A texture the host shows where a document names `host:golden.picture`: the
// path a host takes to put pixels it chose at runtime into a document. Owned by
// the game, so the lease is let go before the asset stack is.
struct HostPicture
{
    std::string Path;
    AssetLease Texture;
    UiHostImageId Image = kInvalidUiHostImage;
};

}  // namespace

// Authored UI, hosted the way a game hosts it: the engine owns the service and
// drives its update, extraction and rendering, so a host only says which
// package to open and what it presents.
//
// It used to build its own UiService to prove a plain application could. The
// engine provides one now, and the document engine's interfaces are
// process-global, so a second would be refused -- which is the right answer: a
// host that wants authored UI asks the engine for it.
class UiHostSystem
{
public:
    UiHostSystem(Engine& engine, std::string packagePath, HostPicture& picture)
        : EnginePtr(&engine)
        , PackagePath(std::move(packagePath))
        , Picture(picture)
    {
    }

    void FrameUpdate(FrameUpdateContext&)
    {
        if (!Initialised)
            Initialise();

        // Where a game's controller would act on what the document asked for.
        // Drained every frame so the queue cannot grow unbounded, and drained
        // here -- before the engine updates the UI -- so a response published
        // in answer lands in the same frame.
        UiService* ui = EnginePtr->TryUi();
        if (ui == nullptr)
            return;
        for (const UiAction& action : ui->DrainActions())
            (void)action;
        PublishPicture();
    }

private:
    void Initialise()
    {
        // Once, and only once: a failed bring-up must not be retried every
        // frame, or the log becomes the failure.
        Initialised = true;

        UiService* ui = EnginePtr->TryUi();
        if (ui == nullptr || !ui->IsReady() || PackagePath.empty())
            return;

        const VkExtent2D extent = EnginePtr->Graphics().Frames.PrimarySwapchain().GetExtent();
        const UiSurfaceId surface =
            ui->CreateSurface("render_host", RenderExtent{ extent.width, extent.height });

        // Registered before the document that names it opens.
        if (!Picture.Path.empty())
        {
            Picture.Texture = EnginePtr->Content().Assets().Assets.LoadLease(Picture.Path, AssetType::Texture);
            Picture.Image = ui->SetHostImage("golden.picture", RenderExtent{ 64, 64 });
        }

        UiScreenDesc desc;
        desc.PackagePath = PackagePath;
        desc.ModelName = "golden";
        desc.Properties = { UiModelProperty{ "health", UiValue(0.0) } };
        desc.Actions = { "golden_ack" };

        Screen = ui->OpenScreen(surface, desc);
        if (!Screen.IsValid())
            return;

        // A fixed value, because a golden image has to be the same every run --
        // but published through the model rather than authored into the
        // document, so the capture is a statement about that whole path.
        (void)ui->SetValue(Screen, UiPropertyIdAt(0), UiValue(210.0));
    }

    // Once the texture is resident; a reload keeps its slot.
    void PublishPicture()
    {
        if (!Picture.Texture.IsValid())
            return;
        const BindlessImageIndex slot = EnginePtr->Content().Assets().Textures->GetBindlessIndex(
            TextureHandle::FromToken(Picture.Texture.OpaqueToken()));
        if (slot.IsValid())
            EnginePtr->Graphics().MainRenderer.GetUiTextures().PublishHostImage(Picture.Image, slot);
    }

    Engine* EnginePtr = nullptr;
    std::string PackagePath;
    HostPicture& Picture;
    UiScreenHandle Screen;
    bool Initialised = false;
};

class RenderHostGame final : public Game
{
public:
    void OnRegisterComponents(ComponentRegistrar&) override {}

    // The behavior the golden rig plays; the one name this host declares.
    void OnRegisterVocabulary(World& world) override
    {
        if (GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>())
            (void)tags->RegisterTag("Anim.Golden.Hold");
    }

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

        engine.Console().Registry().RegisterCVar({
            .Name = "render_host.ui.picture",
            .Owner = "render_host",
            .Type = CVarType::String,
            .DefaultValue = std::string{},
            .CurrentValue = std::string{},
            .Flags = CVarFlags::Transient,
            .Help = "Texture the host shows where the UI names host:golden.picture "
                    "(\"asset://textures/dev/checker.png\"). Empty shows none.",
            .Source = { "render_host" },
            .OnChange = [this](const CVarChangeContext& ctx) {
                Picture.Path = std::get<std::string>(ctx.NewValue);
            },
        });
    }

    void OnRegisterSystems(SystemRegisterContext& ctx) override
    {
        ctx.Schedule.Register<ScriptedCameraPathSystem>(Camera, ScriptedCamera);
        ctx.Schedule.Register<UiHostSystem>(GetEngine(), UiPackagePath, Picture);
    }

    void OnShutdown(GameShutdownContext&) override
    {
        if (Picture.Image != kInvalidUiHostImage)
            GetEngine().Graphics().MainRenderer.GetUiTextures().WithdrawHostImage(Picture.Image);
        Picture.Texture.Reset();
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
    HostPicture Picture;
};

extern "C" SENCHA_GAME_EXPORT Game* SenchaCreateGameModule()
{
    static RenderHostGame instance;
    return &instance;
}

SENCHA_EXPORT_GAME_MODULE_ABI()
