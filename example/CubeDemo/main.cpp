#include <app/Application.h>
#include <app/Engine.h>
#include <app/Game.h>
#include <core/config/EngineConfig.h>
#include <core/logging/LoggingProvider.h>
#include <debug/DebugLogSink.h>
#include <debug/DebugService.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <input/InputFrame.h>
#include <math/Quat.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>
#include <render/Camera.h>
#include <render/Material.h>
#include <render/MeshRendererComponent.h>
#include <render/MeshService.h>
#include <render/RenderQueue.h>
#include <world/entity/EntityRegistry.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformStore.h>
#include <zone/ZoneScene.h>

#ifdef SENCHA_ENABLE_DEBUG_UI
#include <debug/ConsolePanel.h>
#include <debug/IDebugPanel.h>
#include <debug/ImGuiDebugOverlay.h>
#include <debug/TimingPanel.h>
#include <imgui.h>
#endif

#include <SDL3/SDL.h>

#include <cstdio>
#include <memory>

namespace
{
    struct DemoScene
    {
        EntityId Camera;
        EntityId CenterCube;
        MeshHandle CubeMesh;
        MaterialHandle Red;
        MaterialHandle Green;
        MaterialHandle Blue;
    };

    struct FreeCamera
    {
        EntityId Entity;
        float Yaw = 0.0f;
        float Pitch = 0.0f;
        float MoveSpeed = 4.0f;
        float FastMultiplier = 4.0f;
        float MouseSensitivity = 0.0025f;
        bool LookHeld = false;

        void UpdateLook(const InputFrame& input)
        {
            LookHeld = input.IsMouseButtonDown(SDL_BUTTON_RIGHT);
            if (!LookHeld) return;
            Yaw -= input.MouseDeltaX * MouseSensitivity;
            Pitch -= input.MouseDeltaY * MouseSensitivity;
            Pitch = SDL_clamp(Pitch, -1.5f, 1.5f);
        }

        void TickFixed(const InputFrame& input,
                       TransformStore<Transform3f>& transforms,
                       float fixedDt)
        {
            Transform3f* transform = transforms.TryGetLocalMutable(Entity);
            if (transform == nullptr) return;

            Vec3d move = Vec3d::Zero();
            if (input.IsKeyDown(SDL_SCANCODE_W)) move += Vec3d::Forward();
            if (input.IsKeyDown(SDL_SCANCODE_S)) move += Vec3d::Backward();
            if (input.IsKeyDown(SDL_SCANCODE_D)) move += Vec3d::Right();
            if (input.IsKeyDown(SDL_SCANCODE_A)) move += Vec3d::Left();
            if (input.IsKeyDown(SDL_SCANCODE_E)) move += Vec3d::Up();
            if (input.IsKeyDown(SDL_SCANCODE_Q)) move += Vec3d::Down();

            transform->Rotation =
                Quatf::FromAxisAngle(Vec3d::Up(), Yaw)
                * Quatf::FromAxisAngle(Vec3d::Right(), Pitch);

            if (move.SqrMagnitude() > 0.0f)
            {
                move = move.Normalized();
                const float speed = MoveSpeed
                    * (input.IsKeyDown(SDL_SCANCODE_LSHIFT) ? FastMultiplier : 1.0f);
                transform->Position += transform->Rotation.RotateVector(move) * (speed * fixedDt);
            }
        }
    };

    void AddCube(ZoneScene& zone,
                 MeshHandle mesh,
                 MaterialHandle material,
                 const Vec3d& position,
                 const Vec3d& scale)
    {
        EntityId entity = zone.CreateEntity(Transform3f(position, Quatf::Identity(), scale));
        zone.AddMeshRenderer(entity, mesh, material);
    }

    DemoScene CreateScene(ZoneScene& zone,
                          MeshService& meshes,
                          MaterialStore& materials,
                          FreeCamera& freeCamera)
    {
        DemoScene scene;
        scene.CubeMesh = meshes.Create(MeshPrimitives::BuildCube(1.0f));
        scene.Red = materials.Create(Material{
            .Pass = ShaderPassId::ForwardOpaque,
            .BaseColor = Vec4(1.0f, 0.15f, 0.1f, 1.0f),
        });
        scene.Green = materials.Create(Material{
            .Pass = ShaderPassId::ForwardOpaque,
            .BaseColor = Vec4(0.1f, 0.85f, 0.45f, 1.0f),
        });
        scene.Blue = materials.Create(Material{
            .Pass = ShaderPassId::ForwardOpaque,
            .BaseColor = Vec4(0.2f, 0.45f, 1.0f, 1.0f),
        });

        scene.CenterCube = zone.CreateEntity(Transform3f(
            Vec3d(0.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()));
        zone.AddMeshRenderer(scene.CenterCube, scene.CubeMesh, scene.Red);

        AddCube(zone, scene.CubeMesh, scene.Green, Vec3d(-2.2f, 0.0f, -1.0f), Vec3d::One());
        AddCube(zone, scene.CubeMesh, scene.Blue, Vec3d(2.2f, 0.0f, -1.0f), Vec3d(0.75f, 1.5f, 0.75f));
        AddCube(zone, scene.CubeMesh, scene.Green, Vec3d(0.0f, -1.4f, -3.0f), Vec3d(8.0f, 0.15f, 8.0f));

        scene.Camera = zone.CreateEntity(Transform3f(
            Vec3d(0.0f, 1.0f, 5.0f), Quatf::Identity(), Vec3d::One()));
        zone.AddCamera(scene.Camera, CameraComponent{
            .Projection = ProjectionKind::Perspective,
            .FovYRadians = 1.22173048f,
            .NearPlane = 0.1f,
            .FarPlane = 1000.0f,
        });

        freeCamera.Entity = scene.Camera;
        return scene;
    }

#ifdef SENCHA_ENABLE_DEBUG_UI
    class CubeDemoPanel : public IDebugPanel
    {
    public:
        CubeDemoPanel(RenderQueue& queue,
                      CameraRenderData& camera,
                      FreeCamera& freeCamera,
                      ZoneScene& zone,
                      DemoScene& scene)
            : Queue(queue)
            , Camera(camera)
            , FreeCam(freeCamera)
            , Zone(zone)
            , Scene(scene)
        {
        }

        void Draw() override
        {
            ImGui::Begin("Cube Demo");
            ImGui::Text("Right mouse: look");
            ImGui::Text("WASD: move, Q/E: down/up, Shift: fast");
            ImGui::Separator();
            ImGui::Text("Opaque queue: %zu", Queue.Opaque().size());
            ImGui::Text("Camera entity: %u", Camera.Entity.Index);
            ImGui::DragFloat("Move speed", &FreeCam.MoveSpeed, 0.1f, 0.1f, 50.0f);
            ImGui::DragFloat("Mouse sensitivity", &FreeCam.MouseSensitivity, 0.0001f, 0.0001f, 0.02f);

            if (Transform3f* cube = Zone.Transforms().TryGetLocalMutable(Scene.CenterCube))
                ImGui::DragFloat3("Center cube position", &cube->Position.X, 0.05f);

            ImGui::End();
        }

    private:
        RenderQueue& Queue;
        CameraRenderData& Camera;
        FreeCamera& FreeCam;
        ZoneScene& Zone;
        DemoScene& Scene;
    };
#endif

    class CubeDemoGame final : public Game
    {
    public:
        void OnStart(GameStartupContext& ctx) override
        {
            Engine& engine = ctx.EngineInstance;
            ServiceHost& services = engine.Services();
            LoggingProvider& logging = services.GetLoggingProvider();
            DebugService& debug = services.Get<DebugService>();
            DebugLogSink& debugLog = debug.GetLogSink();
            auto& buffers = services.Get<VulkanBufferService>();

            Meshes = std::make_unique<MeshService>(logging, buffers);
            Zone = std::make_unique<ZoneScene>(
                engine.Zones(), ZoneId{ 1 },
                ZoneParticipation{ .Visible = true, .Logic = true });

            Demo = CreateScene(*Zone, *Meshes, Materials, FreeCam);

            engine.RegisterDefaultRenderScene(
                Zone->BuildDefaultRenderScene(*Meshes, Materials));
            engine.AddDefaultMeshRenderFeature(*Meshes, Materials);

#ifdef SENCHA_ENABLE_DEBUG_UI
            auto& windows = services.Get<SdlWindowService>();
            SdlWindow* window = windows.GetPrimaryWindow();
            auto& instance = services.Get<VulkanInstanceService>();
            auto& frames = services.Get<VulkanFrameService>();

            auto debugOverlay =
                std::make_unique<ImGuiDebugOverlay>(debug, *window, instance, frames);
            debugOverlay->AddPanel<ConsolePanel>(debugLog);
            debugOverlay->AddPanel<TimingPanel>(engine.Timing());
            debugOverlay->AddPanel<CubeDemoPanel>(
                engine.GetRenderQueue(), engine.GetCameraData(), FreeCam, *Zone, Demo);
            DebugOverlay = debugOverlay.get();
            auto& renderer = services.Get<Renderer>();
            renderer.AddFeature(std::move(debugOverlay));
#else
            (void)debugLog;
            debug.Open();
#endif

            Zone->ResetPresentationTransforms();
            DiscontinuityToken = engine.Runtime().GetDiscontinuityBus().Subscribe(
                [&](const FrameDiscontinuityEvent&) {
                    Zone->ResetPresentationTransforms();
                });

            std::printf("Sencha Cube Demo\n");
            std::printf("  Right mouse + move: look\n");
            std::printf("  WASD: move, Q/E: down/up, Shift: fast\n");
            std::printf("  F1: pause simulation (timescale 0)\n");
            std::printf("  `: debugger when built with SENCHA_ENABLE_DEBUG_UI=ON\n");
            std::printf("  Escape: quit\n");
        }

        void OnPlatformEvent(PlatformEventContext& ctx) override
        {
#ifdef SENCHA_ENABLE_DEBUG_UI
            if (DebugOverlay != nullptr && DebugOverlay->ProcessSdlEvent(ctx.Event))
                ctx.Handled = true;
#else
            (void)ctx;
#endif
        }

        void OnFixedUpdate(FixedUpdateContext& ctx) override
        {
            Zone->BeginSimulationTick();

            const float fixedDt = static_cast<float>(ctx.Time.DeltaSeconds);
            FreeCam.TickFixed(ctx.Input, Zone->Transforms(), fixedDt);

            if (Transform3f* cube = Zone->Transforms().TryGetLocalMutable(Demo.CenterCube))
                cube->Rotation *= Quatf::FromAxisAngle(Vec3d::Up(), fixedDt);

            Zone->PropagateTransforms();
            Zone->EndSimulationTick();
        }

        void OnExtractRender(RenderExtractContext& ctx) override
        {
            FreeCam.UpdateLook(ctx.Input);
        }

        void OnShutdown(GameShutdownContext& ctx) override
        {
            if (DiscontinuityToken != 0)
            {
                ctx.EngineInstance.Runtime().GetDiscontinuityBus().Unsubscribe(DiscontinuityToken);
                DiscontinuityToken = 0;
            }
            ctx.EngineInstance.RegisterDefaultRenderScene({});
#ifdef SENCHA_ENABLE_DEBUG_UI
            DebugOverlay = nullptr;
#endif
        }

    private:
        std::unique_ptr<ZoneScene> Zone;
        MaterialStore Materials;
        std::unique_ptr<MeshService> Meshes;
        FreeCamera FreeCam;
        DemoScene Demo;
        FrameDiscontinuityToken DiscontinuityToken = 0;
#ifdef SENCHA_ENABLE_DEBUG_UI
        ImGuiDebugOverlay* DebugOverlay = nullptr;
#endif
    };
}

int main(int argc, char** argv)
{
    Application app(argc, argv);
    app.Configure([](EngineConfig& config) {
        config.App.Name = "Sencha Cube Demo";
        config.Window.Title = "Sencha Cube Demo";
        config.Window.Width = 1280;
        config.Window.Height = 720;
        config.Window.GraphicsApi = WindowGraphicsApi::Vulkan;
        config.Runtime.TargetFps = 0.0;
        config.Runtime.ExitOnEscape = true;
        config.Runtime.TogglePauseOnF1 = true;
#ifdef SENCHA_ENABLE_DEBUG_UI
        config.Debug.DebugUi = true;
#endif
    });

    return app.Run<CubeDemoGame>();
}
