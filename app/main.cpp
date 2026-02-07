// main_test2.cpp
// Test #2: One service, three systems, different phases
// Simulate PreUpdate, Update, PostUpdate via ordering priorities.

#include <service/GameServiceHost.h>
#include <system/SystemHost.h>
#include <iostream>

class CounterService : public IService {
public:
    void Add(int v) { counter += v; }
    int Get() const { return counter; }
private:
    int counter = 0;
};

// PreUpdate phase: decide what should happen this frame
class PreUpdateSystem : public ISystem {
public:
    explicit PreUpdateSystem(GameServiceHost& host)
        : counter(host.Get<CounterService>()) {}

    void Update() override {
        // Pre phase sets up a consistent "rule"
        // (for demo, we add 10 before the main update)
        counter.Add(10);
    }

private:
    CounterService& counter;
};

// Update phase: main simulation step
class UpdateSystem : public ISystem {
public:
    explicit UpdateSystem(GameServiceHost& host)
        : counter(host.Get<CounterService>()) {}

    void Update() override {
        // Main phase adds 1
        counter.Add(1);
    }

private:
    CounterService& counter;
};

// PostUpdate phase: observe results, emit events, logging, etc.
class PostUpdateSystem : public ISystem {
public:
    explicit PostUpdateSystem(GameServiceHost& host)
        : counter(host.Get<CounterService>()) {}

    void Update() override {
        std::cout << "Counter: " << counter.Get() << "\n";
    }

private:
    CounterService& counter;
};

int main() {
    GameServiceHost serviceHost;
    SystemHost systemHost;

    serviceHost.AddService<CounterService>();

    // Phase ordering by priority:
    // 0 = PreUpdate, 1 = Update, 2 = PostUpdate
    systemHost.AddSystem<PreUpdateSystem>(0, serviceHost);
    systemHost.AddSystem<UpdateSystem>(1, serviceHost);
    systemHost.AddSystem<PostUpdateSystem>(2, serviceHost);

    systemHost.Init();

    for (int i = 0; i < 5; ++i) {
        systemHost.Update();
    }

    systemHost.Shutdown();
    return 0;
}
