#include <teapot/render/IGraphicsAPI.h>
#include <teapot/render/IRenderable.h>
#include <teapot/render/RenderData.h>
#include <teapot/render/RenderContext.h>
#include <teapot/render/RenderContextService.h>
#include <teapot/render/RenderSystem.h>
#include <teapot/render/RenderSystem2D.h>
#include <teapot/render/RenderSystem3D.h>
#include <teapot/geometry/IGeometry.h>
#include <kettle/batch/RefBatch.h>
#include <kettle/batch/DataBatch.h>
#include <kettle/raii/RefBatchHandle.h>
#include <kettle/service/ServiceHost.h>
#include <kettle/service/ServiceProvider.h>
#include <kettle/system/SystemHost.h>
#include <kettle/logging/ConsoleLogSink.h>
#include <kettle/logging/FileLogSink.h>
#include <iostream>

//=============================================================================
// ConsoleGraphicsAPI
//
// Fake backend that prints frame lifecycle and submit calls to stdout.
//=============================================================================
class ConsoleGraphicsAPI : public IGraphicsAPI
{
public:
    explicit ConsoleGraphicsAPI(const char* name) : Name(name) {}

    bool IsValid() const override { return true; }
    void BeginFrame() override { std::cout << "  [" << Name << "] BeginFrame\n"; }
    void Clear() override     { std::cout << "  [" << Name << "] Clear\n"; }
    void EndFrame() override  { std::cout << "  [" << Name << "] EndFrame\n"; }
    void Present() override   { std::cout << "  [" << Name << "] Present\n"; }

    void Submit2D(const Transform2D& t) override
    {
        std::cout << "    -> Submit2D pos=(" << t.Position.X() << "," << t.Position.Y()
                  << ") scale=(" << t.Scale.X() << "," << t.Scale.Y()
                  << ") rot=" << t.Rotation << "\n";
    }

    void Submit3D(const Transform3D& t) override
    {
        std::cout << "    -> Submit3D pos=(" << t.Position.X() << "," << t.Position.Y() << "," << t.Position.Z()
                  << ") scale=(" << t.Scale.X() << "," << t.Scale.Y() << "," << t.Scale.Z()
                  << ") rot=(" << t.Rotation.X() << "," << t.Rotation.Y() << "," << t.Rotation.Z() << ")\n";
    }

private:
    const char* Name;
};

//=============================================================================
// Custom IRenderable (escape hatch — virtual dispatch path)
//=============================================================================
class DebugOverlay : public IRenderable
{
public:
    void Render(IGraphicsAPI&) override { std::cout << "    -> Draw DebugOverlay\n"; }
    int GetRenderOrder() const override { return 100; }
};

class Game {};

int main()
{
    // -- Services --
    ServiceHost services;

    auto& loggingProvider = services.GetLoggingProvider();
    loggingProvider.SetMinLevel(LogLevel::Debug);
    loggingProvider.AddSink<ConsoleLogSink>();
    loggingProvider.AddSink<FileLogSink>("game.log");

    auto& logger = loggingProvider.GetLogger<Game>();

    // =====================================================================
    // 1. DataBatch-based 2D rendering (cache-friendly DOD path)
    // =====================================================================
    logger.Info("=== DataBatch 2D Rendering Demo ===");

    services.AddService<RenderContextService>(loggingProvider);
    services.AddService<DataBatch<RenderData2D>>();
    services.AddService<DataBatch<RenderData3D>>();
    services.AddService<RefBatch<IRenderable>>();

    auto& contexts = services.Get<RenderContextService>();
    auto& renderables2D = services.Get<DataBatch<RenderData2D>>();
    auto& renderables3D = services.Get<DataBatch<RenderData3D>>();
    auto& customRenderables = services.Get<RefBatch<IRenderable>>();

    ConsoleGraphicsAPI windowA("Window-A");
    uint32_t idA = contexts.AddContext(&windowA);

    // Emplace 2D renderables into the DataBatch — contiguous, cache-friendly
    auto sprite1 = renderables2D.Emplace(RenderData2D{
        .Transform = {{0.2f, 0.3f}, {0.5f, 0.5f}, 0.0f},
        .RenderOrder = 5,
        .bIsVisible = true
    });

    auto sprite2 = renderables2D.Emplace(RenderData2D{
        .Transform = {{-0.4f, 0.1f}, {0.3f, 0.3f}, 0.785f},
        .RenderOrder = 10,
        .bIsVisible = true
    });

    auto hiddenSprite = renderables2D.Emplace(RenderData2D{
        .Transform = {{0.0f, 0.0f}, {1.0f, 1.0f}, 0.0f},
        .RenderOrder = 1,
        .bIsVisible = false
    });

    // Register systems
    ServiceProvider provider(services);
    SystemHost systems;
    systems.AddSystem<RenderSystem2D>(0, provider);
    systems.AddSystem<RenderSystem3D>(1, provider);
    systems.AddSystem<RenderSystem>(2, provider);

    systems.Init();

    std::cout << "=== Frame 1: 2D DataBatch rendering (3 sprites, 1 hidden) ===\n";
    systems.Update();

    // RAII removal — resetting a handle removes from the batch
    std::cout << "\n=== Frame 2: remove sprite2 via RAII handle ===\n";
    sprite2.Reset();
    systems.Update();

    // =====================================================================
    // 2. DataBatch-based 3D rendering
    // =====================================================================
    std::cout << "\n=== Frame 3: add 3D objects ===\n";

    auto cube1 = renderables3D.Emplace(RenderData3D{
        .Transform = {{0.0f, 0.0f, -2.0f}, {0.5f, 0.5f, 0.5f}, {0.0f, 0.785f, 0.0f}},
        .RenderOrder = 0,
        .bIsVisible = true
    });

    auto cube2 = renderables3D.Emplace(RenderData3D{
        .Transform = {{1.0f, 0.5f, -3.0f}, {0.3f, 0.3f, 0.3f}, {0.3f, 0.0f, 0.6f}},
        .RenderOrder = 1,
        .bIsVisible = true
    });

    systems.Update();

    // =====================================================================
    // 3. Custom IRenderable (virtual dispatch escape hatch)
    // =====================================================================
    std::cout << "\n=== Frame 4: add custom IRenderable (debug overlay) ===\n";

    DebugOverlay overlay;
    RefBatchHandle<IRenderable> hOverlay(&customRenderables, &overlay);

    systems.Update();

    // =====================================================================
    // 4. IGeometry service demo
    // =====================================================================
    std::cout << "\n=== IGeometry Service Demo ===\n";

    services.AddService<EuclideanGeometry2D, IGeometry2D>();
    auto& geo = services.Get<IGeometry2D>();

    Vec2 playerPos(0.0f, 0.0f);
    Vec2 enemyPos(3.0f, 4.0f);

    float dist = geo.Distance(playerPos, enemyPos);
    std::cout << "  Distance player->enemy: " << dist << "\n";

    Vec2 dir = geo.Direction(playerPos, enemyPos);
    std::cout << "  Direction: (" << dir.X() << ", " << dir.Y() << ")\n";

    Vec2 moved = geo.MoveToward(playerPos, enemyPos, 2.0f);
    std::cout << "  Move 2 units toward enemy: (" << moved.X() << ", " << moved.Y() << ")\n";

    Vec2 lerped = geo.Interpolate(playerPos, enemyPos, 0.5f);
    std::cout << "  Midpoint: (" << lerped.X() << ", " << lerped.Y() << ")\n";

    logger.Info("All demos complete.");
    systems.Shutdown();
    return 0;
}
