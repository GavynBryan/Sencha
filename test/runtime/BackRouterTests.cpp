#include <gtest/gtest.h>

#include <app/BackRouter.h>

#include <string>
#include <vector>

// Who gets "back out of this", and in what order. The rules that matter are
// the two the shape of the UI decides: a band order that does not depend on
// when anything registered, and a lease that cannot outlive its owner.

TEST(BackRouterTest, NobodyWantingItIsReportedRatherThanSwallowed)
{
    BackRouter router;
    EXPECT_FALSE(router.Dispatch());
}

TEST(BackRouterTest, BandOrderHoldsRegardlessOfRegistrationOrder)
{
    // The engine registers its consumers at startup, before any game screen
    // exists. If order came from registration, every one of them would lose to
    // a screen opened later.
    BackRouter router;
    std::vector<std::string> offered;

    BackConsumerLease fallback = router.AddConsumer("fallback", BackPriority::Fallback,
        [&] { offered.push_back("fallback"); return true; });
    BackConsumerLease diagnostics = router.AddConsumer("diagnostics", BackPriority::Diagnostics,
        [&] { offered.push_back("diagnostics"); return false; });
    BackConsumerLease surface = router.AddConsumer("surface", BackPriority::Surface,
        [&] { offered.push_back("surface"); return false; });
    BackConsumerLease shell = router.AddConsumer("shell", BackPriority::Shell,
        [&] { offered.push_back("shell"); return false; });
    BackConsumerLease text = router.AddConsumer("text", BackPriority::TextEntry,
        [&] { offered.push_back("text"); return false; });

    EXPECT_TRUE(router.Dispatch());
    EXPECT_EQ(offered,
              (std::vector<std::string>{ "diagnostics", "text", "shell", "surface", "fallback" }));
}

TEST(BackRouterTest, TheFirstConsumerToTakeItEndsTheOffer)
{
    BackRouter router;
    bool lowerWasOffered = false;

    BackConsumerLease upper = router.AddConsumer("upper", BackPriority::Shell, [] { return true; });
    BackConsumerLease lower = router.AddConsumer("lower", BackPriority::Surface,
        [&] { lowerWasOffered = true; return true; });

    EXPECT_TRUE(router.Dispatch());
    EXPECT_FALSE(lowerWasOffered);
}

TEST(BackRouterTest, InsideSurfaceTheMostRecentlyRegisteredWins)
{
    // Nested UI: an inventory open since startup spawns a modal. The modal is
    // what Back should close, and it registered second.
    BackRouter router;
    std::vector<std::string> offered;

    BackConsumerLease inventory = router.AddConsumer("inventory", BackPriority::Surface,
        [&] { offered.push_back("inventory"); return true; });
    BackConsumerLease modal = router.AddConsumer("modal", BackPriority::Surface,
        [&] { offered.push_back("modal"); return true; });

    EXPECT_TRUE(router.Dispatch());
    ASSERT_EQ(offered.size(), 1u);
    EXPECT_EQ(offered.front(), "modal");
}

TEST(BackRouterTest, DroppingALeaseUnregistersAndTheConsumerIsNeverCalledAgain)
{
    // A screen destroyed by a load or an error would otherwise leave a lambda
    // holding a dangling `this` for the router to call.
    BackRouter router;
    int calls = 0;

    {
        BackConsumerLease screen = router.AddConsumer("screen", BackPriority::Surface,
            [&] { ++calls; return true; });
        EXPECT_EQ(router.ConsumerCount(), 1u);
        EXPECT_TRUE(router.Dispatch());
        EXPECT_EQ(calls, 1);
    }

    EXPECT_EQ(router.ConsumerCount(), 0u);
    EXPECT_FALSE(router.Dispatch());
    EXPECT_EQ(calls, 1) << "a released consumer was called after its owner went away";
}

TEST(BackRouterTest, AConsumerReleasedByAnEarlierOneIsNotCalled)
{
    // One consumer acting can tear down another -- closing a page drops the
    // lease of what it contained. The walk has to notice.
    BackRouter router;
    bool laterWasCalled = false;
    BackConsumerLease later = router.AddConsumer("later", BackPriority::Surface,
        [&] { laterWasCalled = true; return true; });

    BackConsumerLease earlier = router.AddConsumer("earlier", BackPriority::Shell,
        [&] { later.Reset(); return false; });

    EXPECT_FALSE(router.Dispatch());
    EXPECT_FALSE(laterWasCalled);
}

TEST(BackRouterTest, AConsumerThatDeclinesLetsTheNextOneHaveIt)
{
    // What makes "Back opens the menu" the absence of anyone else wanting it:
    // a text field with nothing being typed into it, a page stack with nothing
    // open, both decline and the fallback gets its turn.
    BackRouter router;
    bool fallbackRan = false;

    BackConsumerLease text = router.AddConsumer("text", BackPriority::TextEntry,
        [] { return false; });
    BackConsumerLease shell = router.AddConsumer("shell", BackPriority::Shell,
        [] { return false; });
    BackConsumerLease fallback = router.AddConsumer("fallback", BackPriority::Fallback,
        [&] { fallbackRan = true; return true; });

    EXPECT_TRUE(router.Dispatch());
    EXPECT_TRUE(fallbackRan);
}

TEST(BackRouterTest, AReleasedSlotIsReusedWithoutResurrectingTheOldConsumer)
{
    BackRouter router;
    int first = 0;
    int second = 0;

    {
        BackConsumerLease one = router.AddConsumer("one", BackPriority::Surface,
            [&] { ++first; return true; });
    }
    BackConsumerLease two = router.AddConsumer("two", BackPriority::Surface,
        [&] { ++second; return true; });

    EXPECT_TRUE(router.Dispatch());
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);
}
