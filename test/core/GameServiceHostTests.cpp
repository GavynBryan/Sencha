#include <gtest/gtest.h>
#include <core/service/ServiceHost.h>

// --- Test fixtures / helpers ---

class StandaloneService : public IService {};

class CountingService : public IService {
public:
    explicit CountingService(int value = 0) : Value(value) {}

    int Value = 0;
};

// --- Tests ---

TEST(ServiceHost, AddAndGetByConcreteType)
{
    ServiceHost host;
    auto& service = host.AddService<StandaloneService>();
    auto& retrieved = host.Get<StandaloneService>();
    EXPECT_EQ(&service, &retrieved);
}

TEST(ServiceHost, AddServiceForwardsConstructorArguments)
{
    ServiceHost host;
    auto& service = host.AddService<CountingService>(7);
    EXPECT_EQ(service.Value, 7);
    EXPECT_EQ(&host.Get<CountingService>(), &service);
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

TEST(ServiceHost, MultipleIndependentServices)
{
    ServiceHost host;
    auto& counting = host.AddService<CountingService>(3);
    auto& standalone = host.AddService<StandaloneService>();

    EXPECT_EQ(&host.Get<CountingService>(), &counting);
    EXPECT_EQ(&host.Get<StandaloneService>(), &standalone);
}
