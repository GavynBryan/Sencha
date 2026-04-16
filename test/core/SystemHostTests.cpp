#include <gtest/gtest.h>
#include <core/system/SystemHost.h>

// --- Test helpers ---

static std::vector<std::string> CallLog;

struct LoggingSystem
{
    explicit LoggingSystem(std::string name) : Name(std::move(name)) {}

    void Init()              { CallLog.push_back(Name + "::Init"); }
    void Update(float)       { CallLog.push_back(Name + "::Update"); }
    void Shutdown()          { CallLog.push_back(Name + "::Shutdown"); }

    std::string Name;
};

struct SystemA : LoggingSystem { SystemA() : LoggingSystem("A") {} };
struct SystemB : LoggingSystem { SystemB() : LoggingSystem("B") {} };
struct SystemC : LoggingSystem { SystemC() : LoggingSystem("C") {} };

struct CountingSystem
{
    int UpdateCount = 0;
    void Update(float) { UpdateCount++; }
};

class SystemHostTest : public ::testing::Test {
protected:
    void SetUp() override { CallLog.clear(); }
};

// --- Tests ---

TEST_F(SystemHostTest, RegisterAndGet)
{
    SystemHost host;
    host.Register<SystemA>();
    auto* system = host.Get<SystemA>();
    EXPECT_NE(system, nullptr);
}

TEST_F(SystemHostTest, GetReturnsNullptrWhenMissing)
{
    SystemHost host;
    EXPECT_EQ(host.Get<SystemA>(), nullptr);
}

TEST_F(SystemHostTest, HasReturnsTrueWhenRegistered)
{
    SystemHost host;
    host.Register<SystemA>();
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
    host.Register<SystemA>();
    host.Register<SystemB>();

    host.Init();

    ASSERT_EQ(CallLog.size(), 2u);
    EXPECT_EQ(CallLog[0], "A::Init");
    EXPECT_EQ(CallLog[1], "B::Init");
}

TEST_F(SystemHostTest, RunFrameCallsUpdateOnAllSystems)
{
    SystemHost host;
    host.Register<SystemA>();
    host.Register<SystemB>();

    host.Init();
    CallLog.clear();
    host.RunFrame(0.0f);

    ASSERT_EQ(CallLog.size(), 2u);
    EXPECT_EQ(CallLog[0], "A::Update");
    EXPECT_EQ(CallLog[1], "B::Update");
}

TEST_F(SystemHostTest, ShutdownCallsInReverseOrder)
{
    SystemHost host;
    host.Register<SystemA>();
    host.Register<SystemB>();

    host.Init();
    CallLog.clear();
    host.Shutdown();

    ASSERT_EQ(CallLog.size(), 2u);
    EXPECT_EQ(CallLog[0], "B::Shutdown");
    EXPECT_EQ(CallLog[1], "A::Shutdown");
}

TEST_F(SystemHostTest, AfterDeclarationOrdersWithinLane)
{
    SystemHost host;
    // Register in reverse desired order — After<> should fix it.
    host.Register<SystemC>();
    host.Register<SystemB>();
    host.Register<SystemA>();

    host.After<SystemB, SystemA>();
    host.After<SystemC, SystemB>();

    host.Init();
    CallLog.clear();
    host.RunFrame(0.0f);

    ASSERT_EQ(CallLog.size(), 3u);
    EXPECT_EQ(CallLog[0], "A::Update");
    EXPECT_EQ(CallLog[1], "B::Update");
    EXPECT_EQ(CallLog[2], "C::Update");
}

TEST_F(SystemHostTest, MultipleRunFrameCalls)
{
    SystemHost host;
    host.Register<CountingSystem>();

    host.Init();
    host.RunFrame(0.0f);
    host.RunFrame(0.0f);
    host.RunFrame(0.0f);

    auto* system = host.Get<CountingSystem>();
    ASSERT_NE(system, nullptr);
    EXPECT_EQ(system->UpdateCount, 3);
}

TEST_F(SystemHostTest, FixedLaneSystemDispatchedByRunFixed)
{
    struct TickSystem {
        int TickCount = 0;
        void Tick(float) { TickCount++; }
    };

    SystemHost host;
    host.Register<TickSystem>();
    host.Init();

    host.RunFixed(1.0f / 60.0f);
    host.RunFixed(1.0f / 60.0f);

    EXPECT_EQ(host.Get<TickSystem>()->TickCount, 2);
}

TEST_F(SystemHostTest, RenderLaneSystemDispatchedByRunRender)
{
    struct RenderSystem {
        float LastAlpha = -1.0f;
        void Render(float alpha) { LastAlpha = alpha; }
    };

    SystemHost host;
    host.Register<RenderSystem>();
    host.Init();

    host.RunRender(0.75f);

    EXPECT_FLOAT_EQ(host.Get<RenderSystem>()->LastAlpha, 0.75f);
}
