#include <gtest/gtest.h>
#include <service/ServiceHost.h>
#include <service/ServiceProvider.h>

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

class TaggedValueService : public IService {
public:
    explicit TaggedValueService(int value = 0) : Value(value) {}

    int Value = 0;
};

struct LocalTransform2Tag {};
struct WorldTransform2Tag {};

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

TEST(ServiceHost, AddTaggedServiceRegistersUnderTaggedKey)
{
    ServiceHost host;
    auto& service = host.AddTaggedService<TaggedValueService, LocalTransform2Tag>(7);

    EXPECT_TRUE((host.HasTagged<TaggedValueService, LocalTransform2Tag>()));
    EXPECT_EQ((host.TryGetTagged<TaggedValueService, LocalTransform2Tag>()), &service);
    EXPECT_EQ((&host.GetTagged<TaggedValueService, LocalTransform2Tag>()), &service);
    EXPECT_EQ((host.GetTagged<TaggedValueService, LocalTransform2Tag>().Value), 7);
}

TEST(ServiceHost, TaggedLookupReturnsNullptrWhenMissing)
{
    ServiceHost host;

    EXPECT_FALSE((host.HasTagged<TaggedValueService, LocalTransform2Tag>()));
    EXPECT_EQ((host.TryGetTagged<TaggedValueService, LocalTransform2Tag>()), nullptr);
    EXPECT_THROW((host.GetTagged<TaggedValueService, LocalTransform2Tag>()), std::runtime_error);
}

TEST(ServiceHost, SameServiceTypeCanCoexistUnderDifferentTags)
{
    ServiceHost host;
    auto& locals = host.AddTaggedService<TaggedValueService, LocalTransform2Tag>(1);
    auto& worlds = host.AddTaggedService<TaggedValueService, WorldTransform2Tag>(2);

    EXPECT_EQ((&host.GetTagged<TaggedValueService, LocalTransform2Tag>()), &locals);
    EXPECT_EQ((&host.GetTagged<TaggedValueService, WorldTransform2Tag>()), &worlds);
    EXPECT_NE(&locals, &worlds);
    EXPECT_EQ((host.GetTagged<TaggedValueService, LocalTransform2Tag>().Value), 1);
    EXPECT_EQ((host.GetTagged<TaggedValueService, WorldTransform2Tag>().Value), 2);
}

TEST(ServiceHost, GetAllTaggedReturnsServicesForOnlyThatTag)
{
    ServiceHost host;
    auto& localA = host.AddTaggedService<TaggedValueService, LocalTransform2Tag>(1);
    auto& localB = host.AddTaggedService<TaggedValueService, LocalTransform2Tag>(2);
    host.AddTaggedService<TaggedValueService, WorldTransform2Tag>(3);

    auto locals = host.GetAllTagged<TaggedValueService, LocalTransform2Tag>();
    ASSERT_EQ(locals.size(), 2u);
    EXPECT_EQ(locals[0], &localA);
    EXPECT_EQ(locals[1], &localB);
}

TEST(ServiceHost, TaggedServicesDoNotRegisterAsUntaggedServices)
{
    ServiceHost host;
    auto& tagged = host.AddTaggedService<TaggedValueService, LocalTransform2Tag>(1);

    EXPECT_FALSE(host.Has<TaggedValueService>());
    EXPECT_EQ(host.TryGet<TaggedValueService>(), nullptr);
    EXPECT_TRUE(host.GetAll<TaggedValueService>().empty());
    EXPECT_THROW(host.Get<TaggedValueService>(), std::runtime_error);

    auto& untagged = host.AddService<TaggedValueService>(2);

    EXPECT_EQ(&host.Get<TaggedValueService>(), &untagged);
    EXPECT_EQ((&host.GetTagged<TaggedValueService, LocalTransform2Tag>()), &tagged);
}

TEST(ServiceProvider, TaggedRetrievalDelegatesToServiceHost)
{
    ServiceHost host;
    auto& service = host.AddTaggedService<TaggedValueService, LocalTransform2Tag>(11);
    ServiceProvider provider(host);

    EXPECT_TRUE((provider.HasTagged<TaggedValueService, LocalTransform2Tag>()));
    EXPECT_FALSE((provider.HasTagged<TaggedValueService, WorldTransform2Tag>()));
    EXPECT_EQ((provider.TryGetTagged<TaggedValueService, LocalTransform2Tag>()), &service);
    EXPECT_EQ((&provider.GetTagged<TaggedValueService, LocalTransform2Tag>()), &service);
    EXPECT_EQ((provider.GetTagged<TaggedValueService, LocalTransform2Tag>().Value), 11);
}

TEST(ServiceProvider, HasAndGetAllDelegateToServiceHost)
{
    ServiceHost host;
    auto& service = host.AddService<TaggedValueService>(5);
    ServiceProvider provider(host);

    EXPECT_TRUE(provider.Has<TaggedValueService>());

    auto all = provider.GetAll<TaggedValueService>();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0], &service);
}
