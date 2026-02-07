#include <service/GameServiceHost.h>
#include <system/SystemHost.h>
#include <iostream>

class IPrintable {
public:
    virtual ~IPrintable() = default;
    virtual void Print() const = 0;
};

class CounterService: public IService, public IPrintable {
public:
    void Increment() {
        counter++;
    }
private:
    int counter = 0;
public:
    void Print() const override {
        std::cout << "Counter: " << counter << std::endl;
    }
};

class IncrementSystem : public ISystem {
public:
    IncrementSystem(GameServiceHost& host)
        : service(host.Get<CounterService>()) {}

    void Update() override {
        service.Increment();
    }
private:
    CounterService& service;
};

class PrintSystem : public ISystem {
public:
    PrintSystem(GameServiceHost& host)
        : printable(host.GetAll<IPrintable>()) {}

    void Update() override {
        for (const auto& p : printable) {
            p->Print();
        }
    }
private:
    std::vector<IPrintable*> printable;
};

int main()
{
    GameServiceHost serviceHost;
    SystemHost systemHost;

    serviceHost.AddService<CounterService, IPrintable>();
    systemHost.AddSystem<IncrementSystem>(0, serviceHost);
    systemHost.AddSystem<PrintSystem>(1, serviceHost);

    systemHost.Init();

    for (int i = 0; i < 5; ++i) {
        systemHost.Update();
    }
    systemHost.Shutdown();
    return 0;
}