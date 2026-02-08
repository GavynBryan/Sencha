#include <render/IGraphicsAPI.h>
#include <render/IRenderable.h>
#include <render/RenderContext.h>
#include <render/RenderContextService.h>
#include <render/RenderSystem.h>
#include <batch/BatchArray.h>
#include <batch/BatchArrayHandle.h>
#include <service/ServiceHost.h>
#include <service/ServiceProvider.h>
#include <system/SystemHost.h>
#include <logging/ConsoleLogSink.h>
#include <logging/FileLogSink.h>
#include <iostream>

//=============================================================================
// ConsoleGraphicsAPI
//
// Fake backend that prints frame lifecycle to stdout.
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

private:
    const char* Name;
};

//=============================================================================
// TriangleRenderable / QuadRenderable
//
// Fake renderables with different render orders and visibility.
//=============================================================================
class TriangleRenderable : public IRenderable
{
public:
    void Render(IGraphicsAPI&) override { std::cout << "    -> Draw Triangle\n"; }
    int GetRenderOrder() const override { return 10; }
};

class QuadRenderable : public IRenderable
{
public:
    QuadRenderable(bool visible) : bVisible(visible) {}

    void Render(IGraphicsAPI&) override { std::cout << "    -> Draw Quad\n"; }
    int GetRenderOrder() const override { return 5; }
    bool IsVisible() const override { return bVisible; }

private:
    bool bVisible;
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

    logger.Info("Starting RenderSystem demo...");

    services.AddService<RenderContextService>(loggingProvider);
    services.AddService<BatchArray<IRenderable>>();

    ServiceProvider provider(services);

    auto& contexts = services.Get<RenderContextService>();
    auto& renderables = services.Get<BatchArray<IRenderable>>();

    // -- Two fake windows --
    ConsoleGraphicsAPI windowA("Window-A");
    ConsoleGraphicsAPI windowB("Window-B");

    uint32_t idA = contexts.AddContext(&windowA);
    uint32_t idB = contexts.AddContext(&windowB);

    // -- Renderables --
    QuadRenderable quad(true);
    TriangleRenderable triangle;
    QuadRenderable hiddenQuad(false);

    BatchArrayHandle hQuad(&renderables, &quad);
    BatchArrayHandle hTriangle(&renderables, &triangle);
    BatchArrayHandle hHidden(&renderables, &hiddenQuad);

    // -- Systems --
    SystemHost systems;
    {
        systems.AddSystem<RenderSystem>(0, provider);
        logger.Info("RenderSystem added to SystemHost.");
    }

    systems.Init();

    std::cout << "=== Frame 1: two windows, three renderables (one hidden) ===\n";
    systems.Update();

    std::cout << "\n=== Frame 2: deactivate Window-B ===\n";
    contexts.GetContext(idB)->bIsActive = false;
    systems.Update();

    std::cout << "\n=== Frame 3: remove triangle, reactivate Window-B ===\n";
    hTriangle.Reset();
    contexts.GetContext(idB)->bIsActive = true;
    systems.Update();

    logger.Info("RenderSystem demo complete.");

    systems.Shutdown();
    return 0;
}
