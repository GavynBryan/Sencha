#include "observers/WeakObserverList.h"
#include "text/PathLeaf.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

TEST(PathLeaf, IsWhatFollowsTheLastSlash)
{
    EXPECT_EQ(PathLeaf("asset://anim/hero.rig.sdata"), "hero.rig.sdata");
    EXPECT_EQ(PathLeaf("hero.sdata"), "hero.sdata");
    EXPECT_EQ(PathLeaf("asset://folder/"), "");
    EXPECT_EQ(PathLeaf(""), "");
}

// An observer stays subscribed as long as its token lives; one subscribed from
// inside a notify hears from the next one on, and the walk survives it.
TEST(WeakObserverList, AnObserverSubscribedWhileNotifyingHearsTheNextOne)
{
    WeakObserverList<int> observers;
    std::vector<int> heard;
    std::vector<std::shared_ptr<WeakObserverList<int>::Fn>> tokens;
    tokens.push_back(observers.Subscribe([&](int value) {
        heard.push_back(value);
        if (tokens.size() < 8)
            tokens.push_back(observers.Subscribe([&](int later) { heard.push_back(later * 10); }));
    }));

    observers.Notify(1);
    EXPECT_EQ(heard, (std::vector<int>{ 1 }));
    observers.Notify(2);
    EXPECT_EQ(heard, (std::vector<int>{ 1, 2, 20 }));
}

TEST(WeakObserverList, DroppingTheTokenUnsubscribes)
{
    WeakObserverList<int> observers;
    int heard = 0;
    auto token = observers.Subscribe([&](int) { ++heard; });
    observers.Notify(0);
    token.reset();
    observers.Notify(0);
    EXPECT_EQ(heard, 1);
}
