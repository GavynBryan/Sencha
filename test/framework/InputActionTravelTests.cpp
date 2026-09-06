#include <gtest/gtest.h>

#include <input/InputAction.h>
#include <input/InputActionState.h>
#include <input/InputActionTravel.h>

// A resolved axis is a displacement plus a held rate. The rate only becomes a
// travel over a duration; reading it as a per-frame travel is how a held stick
// turned twice as fast at twice the frame rate.
namespace
{
    constexpr InputActionId kLook{ 1 };
    constexpr std::size_t kActionCount = 2;

    void Publish(InputActionState& state, Vec2d sum, Vec2d sampled)
    {
        state.FrameStorage()[InputActionRegistry::IndexOf(kLook)] =
            InputActionValue{ static_cast<float>(sum.X), static_cast<float>(sum.Y),
                              InputActionFlags::None };
        state.FrameSampledStorage()[InputActionRegistry::IndexOf(kLook)] =
            InputActionValue{ static_cast<float>(sampled.X), static_cast<float>(sampled.Y),
                              InputActionFlags::None };
    }
}

TEST(InputActionTravel, AHeldStickTurnsByRateTimesTimeAtAnyFrameRate)
{
    InputActionState state;
    state.Configure(kActionCount);
    Publish(state, Vec2d{ 2.4f, 0.0f }, Vec2d{ 2.4f, 0.0f });

    EXPECT_NEAR(FrameAxis2Travel(state, kLook, 1.0 / 60.0).X, 2.4 / 60.0, 1e-6);
    EXPECT_NEAR(FrameAxis2Travel(state, kLook, 1.0 / 120.0).X, 2.4 / 120.0, 1e-6);
}

TEST(InputActionTravel, APointerDisplacementIsATravelWhateverTheFrameTime)
{
    InputActionState state;
    state.Configure(kActionCount);
    Publish(state, Vec2d{ 0.5f, -0.25f }, Vec2d{ 0.0f, 0.0f });

    const Vec2d fast = FrameAxis2Travel(state, kLook, 1.0 / 240.0);
    const Vec2d slow = FrameAxis2Travel(state, kLook, 1.0 / 30.0);
    EXPECT_FLOAT_EQ(fast.X, 0.5f);
    EXPECT_FLOAT_EQ(fast.Y, -0.25f);
    EXPECT_FLOAT_EQ(slow.X, 0.5f);
}

TEST(InputActionTravel, MixedInputSplitsIntoBothKinds)
{
    InputActionState state;
    state.Configure(kActionCount);
    Publish(state, Vec2d{ 3.0f, 0.0f }, Vec2d{ 2.0f, 0.0f });

    const InputAxis2Travel travel = SplitAxis2(state.Frame(), state.FrameSampled(), kLook);
    EXPECT_FLOAT_EQ(travel.Displacement.X, 1.0f);
    EXPECT_FLOAT_EQ(travel.Rate.X, 2.0f);
    EXPECT_FLOAT_EQ(Integrate(travel, 0.5).X, 2.0f);
}
