#include <gtest/gtest.h>
#include <service/GameServiceHost.h>

// --- Test fixtures / helpers ---

class ITestInterface : public IService {
public:
    virtual int Value() const = 0;
};

class TestServiceA : public ITestInterface {
public:
    int Value() const override { return 42; }
};

class TestServiceB : public ITestInterface {
public:
    int Value() const override { return 99; }
};

class StandaloneService : public IService {};

// --- Tests ---

TEST(GameServiceHost, AddAndGetByConcreteType)
{
    GameServiceHost host;
    auto& service = host.AddService<StandaloneService>();
    auto& retrieved = host.Get<StandaloneService>();
    EXPECT_EQ(&service, &retrieved);
}

TEST(GameServiceHost, AddWithInterfaceRegistersUnderBothTypes)
{
    GameServiceHost host;
    auto& service = host.AddService<TestServiceA, ITestInterface>();

    // Retrievable by concrete type
    auto& byConcrete = host.Get<TestServiceA>();
    EXPECT_EQ(&service, &byConcrete);

    // Retrievable by interface type
    auto& byInterface = host.Get<ITestInterface>();
    EXPECT_EQ(&service, &byInterface);
}

TEST(GameServiceHost, TryGetReturnsNullptrWhenMissing)
{
    GameServiceHost host;
    EXPECT_EQ(host.TryGet<StandaloneService>(), nullptr);
}

TEST(GameServiceHost, TryGetReturnsPointerWhenPresent)
{
    GameServiceHost host;
    auto& service = host.AddService<StandaloneService>();
    auto* retrieved = host.TryGet<StandaloneService>();
    EXPECT_EQ(&service, retrieved);
}

TEST(GameServiceHost, HasReturnsFalseWhenMissing)
{
    GameServiceHost host;
    EXPECT_FALSE(host.Has<StandaloneService>());
}

TEST(GameServiceHost, HasReturnsTrueWhenPresent)
{
    GameServiceHost host;
    host.AddService<StandaloneService>();
    EXPECT_TRUE(host.Has<StandaloneService>());
}

TEST(GameServiceHost, GetThrowsWhenMissing)
{
    GameServiceHost host;
    EXPECT_THROW(host.Get<StandaloneService>(), std::runtime_error);
}

TEST(GameServiceHost, GetAllReturnsEmpty)
{
    GameServiceHost host;
    auto result = host.GetAll<ITestInterface>();
    EXPECT_TRUE(result.empty());
}

TEST(GameServiceHost, GetAllReturnsMultipleServices)
{
    GameServiceHost host;
    auto& a = host.AddService<TestServiceA, ITestInterface>();
    auto& b = host.AddService<TestServiceB, ITestInterface>();

    auto all = host.GetAll<ITestInterface>();
    EXPECT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0], &a);
    EXPECT_EQ(all[1], &b);
}

TEST(GameServiceHost, GetAllByInterfacePreservesConcreteIdentity)
{
    GameServiceHost host;
    host.AddService<TestServiceA, ITestInterface>();
    host.AddService<TestServiceB, ITestInterface>();

    auto all = host.GetAll<ITestInterface>();
    EXPECT_EQ(all[0]->Value(), 42);
    EXPECT_EQ(all[1]->Value(), 99);
}

TEST(GameServiceHost, RemoveServiceRemovesFromAllRegistrations)
{
    GameServiceHost host;
    auto& service = host.AddService<TestServiceA, ITestInterface>();

    host.RemoveService(service);

    EXPECT_FALSE(host.Has<TestServiceA>());
    EXPECT_FALSE(host.Has<ITestInterface>());
}

TEST(GameServiceHost, RemoveAllRemovesAllOfType)
{
    GameServiceHost host;
    host.AddService<TestServiceA, ITestInterface>();
    host.AddService<TestServiceB, ITestInterface>();

    host.RemoveAll<ITestInterface>();

    EXPECT_TRUE(host.GetAll<ITestInterface>().empty());
    // Concrete registrations should also be cleaned up
    EXPECT_FALSE(host.Has<TestServiceA>());
    EXPECT_FALSE(host.Has<TestServiceB>());
}

TEST(GameServiceHost, MultipleIndependentServices)
{
    GameServiceHost host;
    auto& a = host.AddService<TestServiceA>();
    auto& standalone = host.AddService<StandaloneService>();

    EXPECT_EQ(&host.Get<TestServiceA>(), &a);
    EXPECT_EQ(&host.Get<StandaloneService>(), &standalone);
}
