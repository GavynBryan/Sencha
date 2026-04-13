#include <gtest/gtest.h>
#include <core/system/SystemHost.h>
#include <core/system/SystemPhase.h>

// --- Test helpers ---

static std::vector<std::string> CallLog;

class LoggingSystem : public ISystem {
    friend class SystemHost;

public:
    LoggingSystem(std::string name) : Name(std::move(name)) {}

private:
    void Init() override    { CallLog.push_back(Name + "::Init"); }
    void Update() override  { CallLog.push_back(Name + "::Update"); }
    void Shutdown() override { CallLog.push_back(Name + "::Shutdown"); }

    std::string Name;
};

class SystemA : public LoggingSystem {
public:
    SystemA() : LoggingSystem("A") {}
};

class SystemB : public LoggingSystem {
public:
    SystemB() : LoggingSystem("B") {}
};

class SystemC : public LoggingSystem {
public:
    SystemC() : LoggingSystem("C") {}
};

class CountingSystem : public ISystem {
    friend class SystemHost;

public:
    int UpdateCount = 0;

private:
    void Update() override { UpdateCount++; }
};

class SystemHostTest : public ::testing::Test {
protected:
    void SetUp() override { CallLog.clear(); }
};

// --- Tests ---

TEST_F(SystemHostTest, AddAndGet)
{
    SystemHost host;
    host.AddSystem<SystemA>(SystemPhase::Update);
    auto* system = host.Get<SystemA>();
    EXPECT_NE(system, nullptr);
}

TEST_F(SystemHostTest, GetReturnsNullptrWhenMissing)
{
    SystemHost host;
    EXPECT_EQ(host.Get<SystemA>(), nullptr);
}

TEST_F(SystemHostTest, HasReturnsTrueWhenAdded)
{
    SystemHost host;
    host.AddSystem<SystemA>(SystemPhase::Update);
    EXPECT_TRUE(host.Has<SystemA>());
}

TEST_F(SystemHostTest, HasReturnsFalseWhenMissing)
{
    SystemHost host;
    EXPECT_FALSE(host.Has<SystemA>());
}

TEST_F(SystemHostTest, InitCallsInitOnAllSystems)
{
    SystemHost host;
    host.AddSystem<SystemA>(SystemPhase::Update);
    host.AddSystem<SystemB>(SystemPhase::PostUpdate);

    host.Init();

    ASSERT_EQ(CallLog.size(), 2u);
    EXPECT_EQ(CallLog[0], "A::Init");
    EXPECT_EQ(CallLog[1], "B::Init");
}

TEST_F(SystemHostTest, UpdateCallsUpdateOnAllSystems)
{
    SystemHost host;
    host.AddSystem<SystemA>(SystemPhase::Update);
    host.AddSystem<SystemB>(SystemPhase::PostUpdate);

    host.Init();
    CallLog.clear();
    host.Update();

    ASSERT_EQ(CallLog.size(), 2u);
    EXPECT_EQ(CallLog[0], "A::Update");
    EXPECT_EQ(CallLog[1], "B::Update");
}

TEST_F(SystemHostTest, ShutdownCallsInReverseOrder)
{
    SystemHost host;
    host.AddSystem<SystemA>(SystemPhase::Update);
    host.AddSystem<SystemB>(SystemPhase::PostUpdate);

    host.Init();
    CallLog.clear();
    host.Shutdown();

    ASSERT_EQ(CallLog.size(), 2u);
    EXPECT_EQ(CallLog[0], "B::Shutdown");
    EXPECT_EQ(CallLog[1], "A::Shutdown");
}

TEST_F(SystemHostTest, SystemsRunInOrder)
{
    SystemHost host;
    // Add in reverse order â€” should still execute by phase
    host.AddSystem<SystemC>(SystemPhase::PreRender);
    host.AddSystem<SystemA>(SystemPhase::Update);
    host.AddSystem<SystemB>(SystemPhase::PostUpdate);

    host.Init();
    CallLog.clear();
    host.Update();

    ASSERT_EQ(CallLog.size(), 3u);
    EXPECT_EQ(CallLog[0], "A::Update");
    EXPECT_EQ(CallLog[1], "B::Update");
    EXPECT_EQ(CallLog[2], "C::Update");
}

TEST_F(SystemHostTest, MultipleUpdates)
{
    SystemHost host;
    host.AddSystem<CountingSystem>(SystemPhase::Update);

    host.Init();
    host.Update();
    host.Update();
    host.Update();

    auto* system = host.Get<CountingSystem>();
    ASSERT_NE(system, nullptr);
    EXPECT_EQ(system->UpdateCount, 3);
}

TEST_F(SystemHostTest, AddSystemAfterInitCallsInitImmediately)
{
    SystemHost host;
    host.AddSystem<SystemA>(SystemPhase::Update);
    host.Init();
    CallLog.clear();

    host.AddSystem<SystemB>(SystemPhase::PostUpdate);

    ASSERT_EQ(CallLog.size(), 1u);
    EXPECT_EQ(CallLog[0], "B::Init");
}
