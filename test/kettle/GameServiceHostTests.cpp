#include <gtest/gtest.h>
#include <kettle/service/ServiceHost.h>

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

TEST(ServiceHost, AddAndGetByConcreteType)
{
    ServiceHost host;
    auto& service = host.AddService<StandaloneService>();
    auto& retrieved = host.Get<StandaloneService>();
    EXPECT_EQ(&service, &retrieved);
}

TEST(ServiceHost, AddWithInterfaceRegistersUnderBothTypes)
{
    ServiceHost host;
    auto& service = host.AddService<TestServiceA, ITestInterface>();

    // Retrievable by concrete type
    auto& byConcrete = host.Get<TestServiceA>();
    EXPECT_EQ(&service, &byConcrete);

    // Retrievable by interface type
    auto& byInterface = host.Get<ITestInterface>();
    EXPECT_EQ(&service, &byInterface);
}

TEST(ServiceHost, TryGetReturnsNullptrWhenMissing)
{
    ServiceHost host;
    EXPECT_EQ(host.TryGet<StandaloneService>(), nullptr);
}

TEST(ServiceHost, TryGetReturnsPointerWhenPresent)
{
    ServiceHost host;
    auto& service = host.AddService<StandaloneService>();
    auto* retrieved = host.TryGet<StandaloneService>();
    EXPECT_EQ(&service, retrieved);
}

TEST(ServiceHost, HasReturnsFalseWhenMissing)
{
    ServiceHost host;
    EXPECT_FALSE(host.Has<StandaloneService>());
}

TEST(ServiceHost, HasReturnsTrueWhenPresent)
{
    ServiceHost host;
    host.AddService<StandaloneService>();
    EXPECT_TRUE(host.Has<StandaloneService>());
}

TEST(ServiceHost, GetThrowsWhenMissing)
{
    ServiceHost host;
    EXPECT_THROW(host.Get<StandaloneService>(), std::runtime_error);
}

TEST(ServiceHost, GetAllReturnsEmpty)
{
    ServiceHost host;
    auto result = host.GetAll<ITestInterface>();
    EXPECT_TRUE(result.empty());
}

TEST(ServiceHost, GetAllReturnsMultipleServices)
{
    ServiceHost host;
    auto& a = host.AddService<TestServiceA, ITestInterface>();
    auto& b = host.AddService<TestServiceB, ITestInterface>();

    auto all = host.GetAll<ITestInterface>();
    EXPECT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0], &a);
    EXPECT_EQ(all[1], &b);
}

TEST(ServiceHost, GetAllByInterfacePreservesConcreteIdentity)
{
    ServiceHost host;
    host.AddService<TestServiceA, ITestInterface>();
    host.AddService<TestServiceB, ITestInterface>();

    auto all = host.GetAll<ITestInterface>();
    EXPECT_EQ(all[0]->Value(), 42);
    EXPECT_EQ(all[1]->Value(), 99);
}

TEST(ServiceHost, RemoveServiceRemovesFromAllRegistrations)
{
    ServiceHost host;
    auto& service = host.AddService<TestServiceA, ITestInterface>();

    host.RemoveService(service);

    EXPECT_FALSE(host.Has<TestServiceA>());
    EXPECT_FALSE(host.Has<ITestInterface>());
}

TEST(ServiceHost, RemoveAllRemovesAllOfType)
{
    ServiceHost host;
    host.AddService<TestServiceA, ITestInterface>();
    host.AddService<TestServiceB, ITestInterface>();

    host.RemoveAll<ITestInterface>();

    EXPECT_TRUE(host.GetAll<ITestInterface>().empty());
    // Concrete registrations should also be cleaned up
    EXPECT_FALSE(host.Has<TestServiceA>());
    EXPECT_FALSE(host.Has<TestServiceB>());
}

TEST(ServiceHost, MultipleIndependentServices)
{
    ServiceHost host;
    auto& a = host.AddService<TestServiceA>();
    auto& standalone = host.AddService<StandaloneService>();

    EXPECT_EQ(&host.Get<TestServiceA>(), &a);
    EXPECT_EQ(&host.Get<StandaloneService>(), &standalone);
}
