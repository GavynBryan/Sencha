#include <core/json/JsonParser.h>
#include <core/logging/ConsoleLogSink.h>
#include <core/logging/LoggingProvider.h>
#include <core/service/ServiceHost.h>
#include <core/system/SystemHost.h>
#include <input/InputActionRegistry.h>
#include <input/InputBindingCompiler.h>
#include <input/InputBindingService.h>
#include <input/SdlInputControlResolver.h>
#include <input/SdlInputSystem.h>
#include <graphics/Renderer.h>
#include <graphics/vulkan/VulkanAllocatorService.h>
#include <graphics/vulkan/VulkanBootstrapPolicy.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDeletionQueueService.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanImageService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanQueueService.h>
#include <graphics/vulkan/VulkanSamplerCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <graphics/vulkan/VulkanSurfaceService.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <graphics/vulkan/VulkanUploadContextService.h>
#include <assets/texture/TextureCache.h>
#include <graphics/features/SpriteFeature.h>
#include <sprite/SpriteRenderSystem.h>
#include <sprite/SpriteComponent.h>
#include <time/TimeService.h>
#include <window/SdlVideoService.h>
#include <window/SdlWindow.h>
#include <window/SdlWindowService.h>
#include <window/WindowCreateInfo.h>
#include <window/WindowTypes.h>
#include <physics/PhysicsSetup2D.h>
#include <world/World.h>
#include <world/World2DSetup.h>
#include <world/World2d.h>
#include <vulkan/vulkan.h>

#include "Player.h"
#include "PlayerSystem.h"

#ifdef SENCHA_ENABLE_DEBUG_UI
#include <debug/ConsolePanel.h>
#include <debug/DebugService.h>
#include <debug/ImGuiDebugOverlay.h>
#endif

#include <fstream>
#include <memory>
#include <span>
#include <sstream>
#include <vector>
#include <chrono>
#include <cstdio>


static std::string ReadTextFile(const char* path)
{
    std::ifstream file(path);
    if (!file) return {};
    std::ostringstream buf;
    buf << file.rdbuf();
    return buf.str();
}

int main()
{
    // =========================================================================
    // Logging
    //
    // LoggingProvider is built into ServiceHost. Add a ConsoleLogSink so
    // engine warnings and errors are visible during development.
    // =========================================================================
    ServiceHost services;
    LoggingProvider& logging = services.GetLoggingProvider();
    logging.AddSink<ConsoleLogSink>();
#ifdef SENCHA_ENABLE_DEBUG_UI
    // Register the debug sink immediately so it captures all startup log output.
    auto& debugLogSink = logging.AddSink<DebugLogSink>();
    debugLogSink.SetMinLevel(LogLevel::Debug);
    auto& debug = services.AddService<DebugService>(logging, debugLogSink);
#endif

    // =========================================================================
    // Window
    //
    // SdlVideoService initializes the SDL video subsystem. SdlWindowService
    // manages the window collection. We create one 1280x720 window and tell
    // it we will use Vulkan for rendering.
    // =========================================================================
    auto& video   = services.AddService<SdlVideoService>(logging);
    auto& windows = services.AddService<SdlWindowService>(logging, video);

    SdlWindow* primaryWindow = windows.CreateWindow({
        .Title       = "Jasmine Green",
        .Width       = 1280,
        .Height      = 720,
        .GraphicsApi = WindowGraphicsApi::Vulkan,
    });

    // =========================================================================
    // Input
    //
    // InputActionRegistry assigns a numeric ID to each named action. The IDs
    // are used at runtime — strings only exist for authoring and debug.
    //
    // InputBindingService holds the compiled binding table: a flat array that
    // maps each SDL scancode to its actions in O(1).
    //
    // The JSON config is compiled into that table here at startup. Any key
    // rebinding in a shipped game would re-run this step and call SetBindings.
    // =========================================================================
    auto& bindingService = services.AddService<InputBindingService>();
    std::vector<std::string> actionNames;
    InputActionRegistry actionRegistry{std::move(actionNames)}; // Will be populated below

    const std::string configJson = ReadTextFile("input_config.json");
    if (!configJson.empty())
    {
        if (auto jsonRoot = JsonParse(configJson))
        {
            if (auto configData = DeserializeInputConfig(*jsonRoot))
            {
                // Extract action names from config and create the registry
                actionNames.clear();
                for (const auto& action : configData->Actions)
                    actionNames.push_back(action.Name);
                actionRegistry = InputActionRegistry{std::move(actionNames)};

                SdlInputControlResolver controlResolver;
                if (auto table = CompileInputBindings(*configData, actionRegistry, controlResolver))
                    bindingService.SetBindings(std::move(*table));
            }
        }
    }

    // =========================================================================
    // Vulkan bootstrap
    //
    // Render features can request device extensions before the Vulkan device
    // is created via Contribute(). We construct SpriteFeature first, let it
    // contribute its requirements to VulkanBootstrapPolicy, then build the
    // entire Vulkan stack with those requirements baked in.
    //
    // Service registration order matters: ServiceHost destroys services in
    // reverse registration order (LIFO). The Renderer must be registered last
    // so it is destroyed first — before the Vulkan services it references are
    // torn down.
    // =========================================================================
    auto spriteFeatureOwned = std::make_unique<SpriteFeature>();

    VulkanBootstrapPolicy policy;
    policy.AppName                = "JasmineGreen";
    policy.RequiredQueues.Present = true;
    spriteFeatureOwned->Contribute(policy);

    for (const char* ext : windows.GetRequiredVulkanInstanceExtensions())
        policy.RequiredInstanceExtensions.push_back(ext);

    auto& instance       = services.AddService<VulkanInstanceService>(logging, policy);
    auto& surface        = services.AddService<VulkanSurfaceService>(logging, instance, *primaryWindow);
    auto& physicalDevice = services.AddService<VulkanPhysicalDeviceService>(logging, instance, policy, &surface);
    auto& device         = services.AddService<VulkanDeviceService>(logging, physicalDevice, policy);
    auto& queues         = services.AddService<VulkanQueueService>(logging, device, physicalDevice, policy);
    auto& swapchain      = services.AddService<VulkanSwapchainService>(
                               logging, device, physicalDevice, surface, queues,
                               windows.GetExtent(windows.GetPrimaryWindowId()));
    auto& upload         = services.AddService<VulkanUploadContextService>(logging, device, queues);
    auto& allocator      = services.AddService<VulkanAllocatorService>(logging, instance, physicalDevice, device);
    auto& buffers        = services.AddService<VulkanBufferService>(logging, device, allocator, upload);
    auto& deletionQueue  = services.AddService<VulkanDeletionQueueService>(logging, 2);
    auto& images         = services.AddService<VulkanImageService>(logging, device, allocator, upload, deletionQueue);
    auto& samplers       = services.AddService<VulkanSamplerCache>(logging, device);
    auto& shaders        = services.AddService<VulkanShaderCache>(logging, device);
    auto& pipelines      = services.AddService<VulkanPipelineCache>(logging, device, shaders);
    auto& descriptors    = services.AddService<VulkanDescriptorCache>(logging, device, buffers, images);
    auto& textures       = services.AddService<TextureCache>(logging, images, descriptors, samplers);
    auto& scratch        = services.AddService<VulkanFrameScratch>(
                               logging, device, physicalDevice, buffers,
                               VulkanFrameScratch::Config{.FramesInFlight = 2, .BytesPerFrame = 64 * 1024 * 1024});
    auto& frames         = services.AddService<VulkanFrameService>(
                               logging, device, queues, swapchain, deletionQueue, 2);

    // Renderer is registered last so it is destroyed before everything above.
    auto& renderer = services.AddService<Renderer>(
        logging, device, physicalDevice, queues, swapchain, frames,
        allocator, buffers, images, samplers, shaders, pipelines, descriptors,
        scratch, upload);

    // Hand the feature to the renderer. AddFeature calls Setup() immediately,
    // caching service pointers and building the sprite pipeline.
    FeatureRef<SpriteFeature> sprites = renderer.AddFeature(std::move(spriteFeatureOwned));

#ifdef SENCHA_ENABLE_DEBUG_UI
    auto debugOverlayOwned = std::make_unique<ImGuiDebugOverlay>(
        debug, *primaryWindow, instance, frames);
    debugOverlayOwned->AddPanel<ConsolePanel>(debug.GetLogSink());
    FeatureRef<ImGuiDebugOverlay> debugOverlay =
        renderer.AddFeature(std::move(debugOverlayOwned));
#endif

    // =========================================================================
    // White pixel texture
    //
    // A 1x1 white RGBA texture is the simplest sprite base: SpriteFeature
    // multiplies the sampled texel by Color, so tinting white gives any flat
    // color without art assets.
    // =========================================================================
    const TextureHandle whitePixel = textures.CreateFromImage(
        Image{.Pixels = {0xFF, 0xFF, 0xFF, 0xFF}, .Width = 1, .Height = 1},
        SamplerDesc{}, "WhitePixel");

    // =========================================================================
    // World — transforms, hierarchy, and physics
    //
    // World2DSetup::Setup2D registers World2d as a service and installs
    // TransformPropagationSystem in the Fixed lane. That system walks the
    // transform hierarchy each fixed step and writes world-space positions
    // into the WorldTransforms batch, which is what Render reads.
    // =========================================================================
    SystemHost systems;
    World2DSetup::Setup2D(services, systems);
    PhysicsSetup2D::Setup(services, systems);
    auto& world = services.Get<World2d>();

    // =========================================================================
    // Player entity
    //
    // Player storage is ordinary dense component storage. EntityRegistry owns
    // handle liveness; component stores are SparseSet-backed and keyed by those
    // handles.
    //
    // The player spawns at the center of the window.
    // =========================================================================
    std::vector<Player> players;
    players.reserve(1);

    const EntityHandle playerBody = world.Entities.Create();
    const EntityHandle playerEye = world.Entities.Create();
    players.emplace_back(
        playerBody,
        playerEye,
        world.Transforms,
        world.Hierarchy,
        Transform2f{ Vec2d{600.0f, 360.0f}, 0.0f, Vec2d{1.0f, 1.0f} },
        world.Bodies,
        world.Sprites,
        whitePixel
    );

    // =========================================================================
    // Systems
    //
    // SdlInputSystem polls SDL events and maps them to InputActionEvents each
    // frame. PlayerSystem reads those events and moves the player.
    //
    // After<PlayerSystem, SdlInputSystem> ensures input is ready before the
    // player tries to read it. Declarations like this are how you express
    // system ordering in Sencha — no per-system priority numbers, just edges.
    // =========================================================================
    auto& inputSystem = systems.Register<SdlInputSystem>(logging, bindingService);
#ifdef SENCHA_ENABLE_DEBUG_UI
    inputSystem.AddSdlEventFilter([debugOverlay](const SDL_Event& event)
    {
        if (auto* overlay = debugOverlay.Get())
            return overlay->ProcessSdlEvent(event);
        return false;
    });
#endif

    const PlayerSystem::Actions playerActions{
        .MoveUp         = *actionRegistry.ResolveAction("MoveUp"),
        .MoveDown       = *actionRegistry.ResolveAction("MoveDown"),
        .MoveLeft       = *actionRegistry.ResolveAction("MoveLeft"),
        .MoveRight      = *actionRegistry.ResolveAction("MoveRight"),
        .ShiftEyeLeft   = *actionRegistry.ResolveAction("ShiftEyeLeft"),
        .ShiftEyeRight  = *actionRegistry.ResolveAction("ShiftEyeRight"),
        .Quit           = *actionRegistry.ResolveAction("Quit"),
    };

    auto& playerSystem = systems.Register<PlayerSystem>(
        inputSystem, players, world.Bodies, world.Transforms, playerActions);

    systems.Register<SpriteRenderSystem>(world.Sprites, world.Transforms, *sprites, textures);

    systems.After<PlayerSystem, SdlInputSystem>();
    systems.Init();

    // =========================================================================
    // Time
    // =========================================================================
    auto& time = services.AddService<TimeService>();

    // =========================================================================
    // Game loop
    //
    // Each iteration of the loop:
    //   1. RunFrame  — input + gameplay (variable dt)
    //   2. RunFixed  — physics + transform propagation (same dt here for
    //                  simplicity; a production game uses a fixed accumulator)
    //   3. RunRender — submit draw calls for this frame
    //   4. DrawFrame — GPU acquire → record → present
    //
    // Press Escape to quit. Window close events are not yet forwarded through
    // the input pipeline — that will be addressed in a future engine update.
    // =========================================================================
    using Clock = std::chrono::steady_clock;
    using Ms = std::chrono::duration<double, std::milli>;
    int frameCount = 0;
    double tFrame = 0, tFixed = 0, tRender = 0, tDraw = 0;

    while (!playerSystem.WantsQuit() && !inputSystem.IsQuitRequested())
    {
        const FrameClock clock = time.Advance();

        auto t0 = Clock::now();
        systems.RunFrame(clock.Dt);
        auto t1 = Clock::now();
        systems.RunFixed(clock.Dt);
        auto t2 = Clock::now();

        const WindowExtent extent = windows.GetExtent(windows.GetPrimaryWindowId());
        if (extent.Width == 0 || extent.Height == 0)
            continue;

        systems.RunRender(1.0f);
        auto t3 = Clock::now();

        const auto drawStatus = renderer.DrawFrame();
        auto t4 = Clock::now();

        tFrame  += Ms(t1 - t0).count();
        tFixed  += Ms(t2 - t1).count();
        tRender += Ms(t3 - t2).count();
        tDraw   += Ms(t4 - t3).count();
        if (++frameCount == 120)
        {
            std::printf("avg over 120 frames  frame=%.2fms  fixed=%.2fms  render=%.2fms  draw=%.2fms\n",
                tFrame/120, tFixed/120, tRender/120, tDraw/120);
            tFrame = tFixed = tRender = tDraw = 0;
            frameCount = 0;
        }
        if (drawStatus == Renderer::DrawStatus::SwapchainOutOfDate)
        {
            swapchain.Recreate(windows.GetExtent(windows.GetPrimaryWindowId()));
            renderer.NotifySwapchainRecreated();
        }
    }

    // Wait for the GPU to finish all in-flight work before tearing down
    // Vulkan resources. Without this wait, the destructor chain could free
    // buffers that the GPU is still reading.
    vkDeviceWaitIdle(device.GetDevice());

    systems.Shutdown();
    return 0;
}
