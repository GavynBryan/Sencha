// Announcing and delivering authored events: publishing only queues, one drain
// delivers first in first out and keeps going until nothing is left, a chain
// of reactions finishes inside one drain without nesting calls, and a chain
// that feeds itself is stopped with its trace kept.

#include "AllocationCounter.h"

#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredEventDispatcher.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/VerbDispatcher.h>
#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// Events as the generator writes them: a name and a payload encoder.
struct TestRotated
{
};
template<>
struct AuthoredApiDefinition<TestRotated>
{
    static constexpr std::string_view EventName = "test.rotated";
    static void Encode(const TestRotated&, AuthoredArguments& payload) { payload.Resize(0); }
};

struct TestPing
{
    std::int64_t Count = 0;
};
template<>
struct AuthoredApiDefinition<TestPing>
{
    static constexpr std::string_view EventName = "test.ping";
    static void Encode(const TestPing& event, AuthoredArguments& payload)
    {
        payload.Resize(1);
        payload.Set(0, AuthoredValue::Int(event.Count));
    }
};

struct TestOther
{
};
template<>
struct AuthoredApiDefinition<TestOther>
{
    static constexpr std::string_view EventName = "test.other";
    static void Encode(const TestOther&, AuthoredArguments& payload) { payload.Resize(0); }
};

// Has a companion, but nothing declared it into the catalog under test.
struct TestUndeclared
{
};
template<>
struct AuthoredApiDefinition<TestUndeclared>
{
    static constexpr std::string_view EventName = "test.undeclared";
    static void Encode(const TestUndeclared&, AuthoredArguments& payload) { payload.Resize(0); }
};

namespace
{
[[nodiscard]] AuthoredEventDefinition Event(std::string name, bool counted = false)
{
    AuthoredEventDefinition definition;
    definition.Name = std::move(name);
    if (counted)
    {
        DataFieldSchema count;
        count.Key = "count";
        count.Kind = DataFieldKind::Int;
        definition.Payload.Children.push_back(std::move(count));
    }
    return definition;
}

// Records every delivery it hears, in the order it heard them.
struct Listener
{
    int Tag = 0;
    std::vector<int>* Order = nullptr;
    std::vector<AuthoredEventDelivery> Heard{};

    static void Deliver(Listener& self, const AuthoredEventDelivery& delivery)
    {
        self.Heard.push_back(delivery);
        if (self.Order != nullptr)
            self.Order->push_back(self.Tag);
    }
};

class AuthoredEventDispatchTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        AuthoredEventRegistrationScope scope(Registry, "test");
        (void)scope.Declare(Event("test.rotated"));
        (void)scope.Declare(Event("test.ping", true));
        (void)scope.Declare(Event("test.other"));
        ASSERT_TRUE(scope.Commit());
        Rotated = Registry.Find("test.rotated");
        Ping = Registry.Find("test.ping");
        Other = Registry.Find("test.other");
        RotatedEvent = Registry.Resolve("test.rotated");
        PingEvent = Registry.Resolve("test.ping");
        OtherEvent = Registry.Resolve("test.other");
        Events.emplace(Registry, Logging.GetLogger<AuthoredEventDispatchTest>());
    }

    LoggingProvider Logging;
    AuthoredEventRegistry Registry;
    AuthoredEventId Rotated;
    AuthoredEventId Ping;
    AuthoredEventId Other;
    AuthoredEventHandle RotatedEvent;
    AuthoredEventHandle PingEvent;
    AuthoredEventHandle OtherEvent;
    std::optional<AuthoredEventDispatcher> Events;
};
} // namespace

TEST_F(AuthoredEventDispatchTest, PublishingOnlyQueues)
{
    Listener listener;
    AuthoredEventSubscription subscription =
        Events->Subscribe<&Listener::Deliver>(OtherEvent, EntityId{}, listener);
    ASSERT_TRUE(Events->Publish(EntityId{}, TestOther{}));
    EXPECT_TRUE(listener.Heard.empty()) << "a subscriber ran inside Publish";
    EXPECT_EQ(Events->Queued(), 1u);

    const AuthoredEventDrainResult result = Events->Drain(7);
    EXPECT_EQ(result.Delivered, 1u);
    ASSERT_EQ(listener.Heard.size(), 1u);
    EXPECT_EQ(listener.Heard.front().Tick, 7u);
}

TEST_F(AuthoredEventDispatchTest, OccurrencesAreFirstInFirstOutAndSubscribersHearInSubscriptionOrder)
{
    std::vector<int> order;
    Listener first{ .Tag = 1, .Order = &order };
    Listener second{ .Tag = 2, .Order = &order };
    AuthoredEventSubscription a = Events->Subscribe<&Listener::Deliver>(PingEvent, EntityId{}, first);
    AuthoredEventSubscription b = Events->Subscribe<&Listener::Deliver>(PingEvent, EntityId{}, second);

    (void)Events->Publish(EntityId{}, TestPing{ .Count = 10 });
    (void)Events->Publish(EntityId{}, TestPing{ .Count = 20 });
    (void)Events->Drain(1);

    EXPECT_EQ(order, (std::vector<int>{ 1, 2, 1, 2 }));
    ASSERT_EQ(first.Heard.size(), 2u);
    EXPECT_LT(first.Heard[0].Sequence.Value, first.Heard[1].Sequence.Value);
}

TEST_F(AuthoredEventDispatchTest, ASourceFilterHearsOnlyItsEntity)
{
    const EntityId a{ .Index = 1, .Generation = 1 };
    const EntityId b{ .Index = 2, .Generation = 1 };
    Listener onlyA;
    Listener everyone;
    AuthoredEventSubscription filtered = Events->Subscribe<&Listener::Deliver>(OtherEvent, a, onlyA);
    AuthoredEventSubscription open = Events->Subscribe<&Listener::Deliver>(OtherEvent, EntityId{}, everyone);

    (void)Events->Publish(a, TestOther{});
    (void)Events->Publish(b, TestOther{});
    (void)Events->Drain(1);
    ASSERT_EQ(onlyA.Heard.size(), 1u);
    EXPECT_EQ(onlyA.Heard.front().Source, a);
    EXPECT_EQ(everyone.Heard.size(), 2u);
}

namespace
{
// A verb whose operation acts at once and announces what it did: rotating an
// entity is an immediate change, and the announcement follows the change.
class Rotator
{
public:
    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        EntityId target;
        if (!invocation.Arguments->TryGetEntity(0, target))
            return VerbAdmission::InvalidArguments;
        Rotations.push_back(Rotation{ target, invocation.Id, invocation.Parent });
        (void)Events->Publish(target, TestRotated{}, invocation.Id);
        return VerbAdmission::Accepted;
    }

    struct Rotation
    {
        EntityId Target;
        InvocationId Id;
        InvocationId Parent;
    };
    AuthoredEventDispatcher* Events = nullptr;
    std::vector<Rotation> Rotations;
};

// Reacts to one entity's rotation by rotating the next, through the verb and
// never by calling the rotator: what a compiled graph does.
struct Reaction
{
    VerbDispatcher* Verbs = nullptr;
    const CompiledVerbBinding* Rotate = nullptr;
    std::vector<std::pair<EntityId, EntityId>> Links{};
    std::vector<VerbAdmission> Admissions{};
    std::vector<AuthoredEventDelivery> Heard{};

    static void Deliver(Reaction& self, const AuthoredEventDelivery& delivery)
    {
        self.Heard.push_back(delivery);
        for (const auto& [from, to] : self.Links)
        {
            if (from != delivery.Source)
                continue;
            const AuthoredValue next = AuthoredValue::Entity(to);
            VerbInvocationSource source;
            source.Parent = delivery.Cause.Invocation;
            self.Admissions.push_back(self.Verbs->Invoke(*self.Rotate, { &next, 1 }, source).Status);
        }
    }
};
} // namespace

TEST_F(AuthoredEventDispatchTest, AChainOfReactionsCompletesInOneDrainWithoutNesting)
{
    VerbRegistry verbs;
    {
        VerbDefinition rotate;
        rotate.Name = "test.rotate";
        DataFieldSchema target;
        target.Key = "target";
        target.Kind = DataFieldKind::Entity;
        rotate.Arguments.Children.push_back(std::move(target));
        VerbRegistrationScope scope(verbs, "test");
        (void)scope.Declare(std::move(rotate));
        ASSERT_TRUE(scope.Commit());
    }
    VerbDispatcher dispatcher(verbs);
    Rotator rotator;
    rotator.Events = &*Events;
    VerbBindingToken token = dispatcher.Bind(verbs.Find("test.rotate"), rotator);

    VerbBindingDesc desc;
    desc.Key = "rotate";
    desc.KeyId = MakeVerbBindingKey(desc.Key);
    desc.VerbName = "test.rotate";
    desc.Inputs = { "which" };
    VerbBindingArgument which;
    which.Key = "target";
    which.Source = VerbArgumentSource::Input;
    which.Text = "which";
    desc.Arguments.push_back(std::move(which));
    std::vector<std::string> errors;
    CompiledVerbBinding rotate;
    ASSERT_TRUE(CompileVerbBinding(desc, VerbBindingEnvironment{ .Verbs = &verbs }, rotate, errors));

    const EntityId a{ .Index = 1, .Generation = 1 };
    const EntityId b{ .Index = 2, .Generation = 1 };
    const EntityId c{ .Index = 3, .Generation = 1 };
    Reaction reaction{ .Verbs = &dispatcher, .Rotate = &rotate, .Links = { { a, b }, { b, c } } };
    AuthoredEventSubscription subscription =
        Events->Subscribe<&Reaction::Deliver>(RotatedEvent, EntityId{}, reaction);

    // Rotate A from outside any drain, the way a system or a UI would.
    const AuthoredValue start = AuthoredValue::Entity(a);
    ASSERT_EQ(dispatcher.Invoke(rotate, { &start, 1 }).Status, VerbAdmission::Accepted);
    ASSERT_EQ(rotator.Rotations.size(), 1u);

    const AuthoredEventDrainResult result = Events->Drain(1);
    EXPECT_FALSE(result.BudgetExceeded);
    EXPECT_EQ(result.Remaining, 0u);

    // A, B and C, all in the one drain, and no request refused as reentrant.
    ASSERT_EQ(rotator.Rotations.size(), 3u);
    EXPECT_EQ(rotator.Rotations[1].Target, b);
    EXPECT_EQ(rotator.Rotations[2].Target, c);
    for (const VerbAdmission admission : reaction.Admissions)
        EXPECT_EQ(admission, VerbAdmission::Accepted);

    // The chain is traceable end to end: each rotation names the one whose
    // announcement caused it, and every announcement shares the first's root.
    EXPECT_EQ(rotator.Rotations[1].Parent, rotator.Rotations[0].Id);
    EXPECT_EQ(rotator.Rotations[2].Parent, rotator.Rotations[1].Id);
    ASSERT_EQ(reaction.Heard.size(), 3u);
    EXPECT_EQ(reaction.Heard[1].Root, reaction.Heard[0].Sequence);
    EXPECT_EQ(reaction.Heard[2].Root, reaction.Heard[0].Sequence);
    EXPECT_EQ(reaction.Heard[1].Cause.Event, reaction.Heard[0].Sequence);
    EXPECT_EQ(reaction.Heard[2].Cause.Invocation, rotator.Rotations[2].Id);
}

namespace
{
// A reaction that announces the event it reacts to: work that never ends.
struct Echo
{
    AuthoredEventDispatcher* Events = nullptr;
    int Calls = 0;

    static void Deliver(Echo& self, const AuthoredEventDelivery&)
    {
        ++self.Calls;
        (void)self.Events->Publish(EntityId{}, TestPing{ .Count = self.Calls });
    }
};
} // namespace

TEST_F(AuthoredEventDispatchTest, ARunawayChainIsQuarantinedOnTheSecondExhaustedDrainByDefault)
{
    Events->SetBudget(10);
    Echo echo{ .Events = &*Events };
    AuthoredEventSubscription loop = Events->Subscribe<&Echo::Deliver>(PingEvent, EntityId{}, echo);
    Listener bystander;
    AuthoredEventSubscription other = Events->Subscribe<&Listener::Deliver>(OtherEvent, EntityId{}, bystander);

    (void)Events->Publish(EntityId{}, TestPing{});
    const AuthoredEventDrainResult first = Events->Drain(1);
    EXPECT_TRUE(first.BudgetExceeded);
    EXPECT_EQ(first.Remaining, 1u) << "what was left is kept for the next drain";
    EXPECT_EQ(first.QuarantinedRoots, 0u) << "one exhausted drain is a burst, not yet a runaway";
    EXPECT_EQ(Events->LastQuarantine(), nullptr);

    // Unrelated work queued behind the runaway chain.
    (void)Events->Publish(EntityId{}, TestOther{});
    const AuthoredEventDrainResult second = Events->Drain(2);
    EXPECT_TRUE(second.BudgetExceeded);
    EXPECT_EQ(second.QuarantinedRoots, 1u);
    EXPECT_EQ(second.Remaining, 0u) << "the quarantined chain's occurrences were not discarded";
    EXPECT_EQ(bystander.Heard.size(), 1u) << "other traffic was starved by the runaway chain";

    const AuthoredEventQuarantine* quarantine = Events->LastQuarantine();
    ASSERT_NE(quarantine, nullptr);
    EXPECT_EQ(quarantine->Tick, 2u);
    EXPECT_GE(quarantine->Discarded, 1u);
    ASSERT_FALSE(quarantine->Trace.empty());
    EXPECT_EQ(quarantine->Trace.back().Event, Ping);
    EXPECT_EQ(quarantine->Trace.back().Root, quarantine->Root);

    // The chain is stopped: nothing of it is left to run.
    const int callsBefore = echo.Calls;
    const AuthoredEventDrainResult third = Events->Drain(3);
    EXPECT_EQ(third.Delivered, 0u);
    EXPECT_EQ(echo.Calls, callsBefore);
}

TEST_F(AuthoredEventDispatchTest, ABurstThatDrainsCleanlyNextTimeIsNeverQuarantined)
{
    Events->SetBudget(10);
    Listener listener;
    AuthoredEventSubscription subscription =
        Events->Subscribe<&Listener::Deliver>(OtherEvent, EntityId{}, listener);
    for (int index = 0; index < 15; ++index)
        (void)Events->Publish(EntityId{}, TestOther{});

    EXPECT_TRUE(Events->Drain(1).BudgetExceeded);
    const AuthoredEventDrainResult second = Events->Drain(2);
    EXPECT_FALSE(second.BudgetExceeded);
    EXPECT_EQ(second.QuarantinedRoots, 0u);
    EXPECT_EQ(listener.Heard.size(), 15u);
    EXPECT_EQ(Events->LastQuarantine(), nullptr);
}

TEST_F(AuthoredEventDispatchTest, AFullQueueRefusesRatherThanDropping)
{
    Events->SetCapacity(2);
    EXPECT_TRUE(Events->Publish(EntityId{}, TestOther{}));
    EXPECT_TRUE(Events->Publish(EntityId{}, TestOther{}));
    EXPECT_FALSE(Events->Publish(EntityId{}, TestOther{}));
    EXPECT_EQ(Events->RefusedCount(), 1u);
    EXPECT_EQ(Events->Drain(1).Delivered, 2u);
}

TEST_F(AuthoredEventDispatchTest, AnEventThisCatalogNeverDeclaredIsRefused)
{
    EXPECT_FALSE(Events->Publish(EntityId{}, TestUndeclared{}));
    EXPECT_EQ(Events->Queued(), 0u);
    EXPECT_EQ(Events->RefusedCount(), 1u);
}

namespace
{
struct Nester
{
    AuthoredEventDispatcher* Events = nullptr;
    std::size_t NestedDelivered = 99;

    static void Deliver(Nester& self, const AuthoredEventDelivery&)
    {
        self.NestedDelivered = self.Events->Drain(0).Delivered;
    }
};
} // namespace

TEST_F(AuthoredEventDispatchTest, ADrainFromInsideADeliveryIsRefused)
{
    Nester nester{ .Events = &*Events };
    AuthoredEventSubscription subscription =
        Events->Subscribe<&Nester::Deliver>(OtherEvent, EntityId{}, nester);
    (void)Events->Publish(EntityId{}, TestOther{});
    (void)Events->Publish(EntityId{}, TestOther{});
    EXPECT_EQ(Events->Drain(1).Delivered, 2u);
    EXPECT_EQ(nester.NestedDelivered, 0u);
}

namespace
{
// Subscribes a late listener, and removes another, while being delivered to.
struct Rearranger
{
    AuthoredEventDispatcher* Events = nullptr;
    AuthoredEventHandle Event;
    Listener* Late = nullptr;
    AuthoredEventSubscription* Doomed = nullptr;
    AuthoredEventSubscription LateSubscription{};

    static void Deliver(Rearranger& self, const AuthoredEventDelivery&)
    {
        if (self.Doomed != nullptr)
        {
            self.Doomed->Reset();
            self.Doomed = nullptr;
            self.LateSubscription =
                self.Events->Subscribe<&Listener::Deliver>(self.Event, EntityId{}, *self.Late);
        }
    }
};
} // namespace

TEST_F(AuthoredEventDispatchTest, SubscriptionsChangedDuringADeliveryApplyFromTheNextOccurrence)
{
    Listener late;
    Listener doomed;
    Rearranger rearranger{ .Events = &*Events, .Event = OtherEvent, .Late = &late };
    AuthoredEventSubscription first =
        Events->Subscribe<&Rearranger::Deliver>(OtherEvent, EntityId{}, rearranger);
    AuthoredEventSubscription doomedSubscription =
        Events->Subscribe<&Listener::Deliver>(OtherEvent, EntityId{}, doomed);
    rearranger.Doomed = &doomedSubscription;

    (void)Events->Publish(EntityId{}, TestOther{});
    (void)Events->Publish(EntityId{}, TestOther{});
    (void)Events->Drain(1);

    // Removed before its turn in the first occurrence: never heard anything.
    EXPECT_TRUE(doomed.Heard.empty());
    // Added during the first occurrence: heard only the second.
    ASSERT_EQ(late.Heard.size(), 1u);
    EXPECT_EQ(late.Heard.front().Sequence.Value, 2u);
}

namespace
{
// Announces more while it reads the payload it was handed, which must not move.
struct Crowder
{
    AuthoredEventDispatcher* Events = nullptr;
    std::vector<std::int64_t> Read{};

    static void Deliver(Crowder& self, const AuthoredEventDelivery& delivery)
    {
        std::int64_t before = -1;
        (void)delivery.Payload->TryGetInt(0, before);
        if (before == 1)
        {
            for (std::int64_t index = 0; index < 8; ++index)
                (void)self.Events->Publish(EntityId{}, TestPing{ .Count = 100 + index });
        }
        std::int64_t after = -1;
        (void)delivery.Payload->TryGetInt(0, after);
        self.Read.push_back(after == before ? after : -1);
    }
};
} // namespace

TEST_F(AuthoredEventDispatchTest, APayloadStaysValidWhileItsSubscriberPublishes)
{
    Events->SetCapacity(16);
    Crowder crowder{ .Events = &*Events };
    AuthoredEventSubscription subscription =
        Events->Subscribe<&Crowder::Deliver>(PingEvent, EntityId{}, crowder);
    (void)Events->Publish(EntityId{}, TestPing{ .Count = 1 });
    (void)Events->Drain(1);
    ASSERT_EQ(crowder.Read.size(), 9u);
    EXPECT_EQ(crowder.Read.front(), 1);
    EXPECT_EQ(crowder.Read.back(), 107);
}

TEST_F(AuthoredEventDispatchTest, ASubscriptionOutlivingItsDispatcherIsInert)
{
    Listener listener;
    AuthoredEventSubscription subscription =
        Events->Subscribe<&Listener::Deliver>(OtherEvent, EntityId{}, listener);
    Events.reset();
    EXPECT_FALSE(subscription.IsValid());
    subscription.Reset();
    SUCCEED();
}

TEST_F(AuthoredEventDispatchTest, AClosedDispatcherRefusesAnnouncements)
{
    Events->CloseAdmission();
    EXPECT_FALSE(Events->Publish(EntityId{}, TestOther{}));
}

TEST_F(AuthoredEventDispatchTest, AWarmedPublishAndDrainAllocateNothing)
{
    Listener listener;
    AuthoredEventSubscription subscription =
        Events->Subscribe<&Listener::Deliver>(PingEvent, EntityId{}, listener);
    listener.Heard.reserve(20000);
    // Warm every slot's payload storage once.
    for (std::size_t round = 0; round < 2; ++round)
    {
        for (std::size_t index = 0; index < Events->Capacity(); ++index)
            (void)Events->Publish(EntityId{}, TestPing{ .Count = 1 });
        (void)Events->Drain(round);
    }
    listener.Heard.clear();

    constexpr int kRounds = 100;
    constexpr int kPerRound = 100;
    const std::size_t before = AllocationCount();
    const auto start = std::chrono::steady_clock::now();
    for (int round = 0; round < kRounds; ++round)
    {
        for (int index = 0; index < kPerRound; ++index)
            (void)Events->Publish(EntityId{}, TestPing{ .Count = index });
        (void)Events->Drain(static_cast<std::uint64_t>(round));
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_EQ(AllocationCount(), before) << "a warmed scalar publish and drain allocated";
    EXPECT_EQ(listener.Heard.size(), static_cast<std::size_t>(kRounds * kPerRound));
    std::printf("authored events: %.1f ns per published and delivered occurrence (this build)\n",
                std::chrono::duration<double, std::nano>(elapsed).count() / (kRounds * kPerRound));
}

namespace
{
// A finite chain: each occurrence announces the next until its count runs out.
struct Countdown
{
    AuthoredEventDispatcher* Events = nullptr;
    int Heard = 0;

    static void Deliver(Countdown& self, const AuthoredEventDelivery& delivery)
    {
        ++self.Heard;
        std::int64_t count = 0;
        (void)delivery.Payload->TryGetInt(0, count);
        if (count > 0)
            (void)self.Events->Publish(EntityId{}, TestPing{ .Count = count - 1 });
    }
};
} // namespace

TEST_F(AuthoredEventDispatchTest, TheCutoffIsForSustainedWorkAndItsThresholdIsConfigurable)
{
    Events->SetBudget(10);
    Countdown chain{ .Events = &*Events };
    AuthoredEventSubscription subscription =
        Events->Subscribe<&Countdown::Deliver>(PingEvent, EntityId{}, chain);

    // Twenty-six occurrences: finite, but more than two budgets' worth. The
    // default cuts it off -- it is not a cycle, and nothing claims it is.
    (void)Events->Publish(EntityId{}, TestPing{ .Count = 25 });
    (void)Events->Drain(1);
    EXPECT_EQ(Events->Drain(2).QuarantinedRoots, 1u);
    EXPECT_EQ(chain.Heard, 20);

    // Allowed a third exhausted drain, the same chain finishes.
    Events->SetQuarantineAfter(3);
    chain.Heard = 0;
    (void)Events->Publish(EntityId{}, TestPing{ .Count = 25 });
    (void)Events->Drain(3);
    (void)Events->Drain(4);
    const AuthoredEventDrainResult last = Events->Drain(5);
    EXPECT_FALSE(last.BudgetExceeded);
    EXPECT_EQ(last.QuarantinedRoots, 0u);
    EXPECT_EQ(chain.Heard, 26);
}

namespace
{
// Reacts to every ping with one announcement of its own.
struct Echoer
{
    AuthoredEventDispatcher* Events = nullptr;
    int Accepted = 0;

    static void Deliver(Echoer& self, const AuthoredEventDelivery&)
    {
        if (self.Events->Publish(EntityId{}, TestOther{}))
            ++self.Accepted;
    }
};
} // namespace

TEST_F(AuthoredEventDispatchTest, AReactionIsQueuedEvenWhenTheQueueStartedFull)
{
    Events->SetCapacity(2);
    Echoer echoer{ .Events = &*Events };
    AuthoredEventSubscription subscription =
        Events->Subscribe<&Echoer::Deliver>(PingEvent, EntityId{}, echoer);
    Listener after;
    AuthoredEventSubscription others =
        Events->Subscribe<&Listener::Deliver>(OtherEvent, EntityId{}, after);

    ASSERT_TRUE(Events->Publish(EntityId{}, TestPing{}));
    ASSERT_TRUE(Events->Publish(EntityId{}, TestPing{}));
    ASSERT_FALSE(Events->Publish(EntityId{}, TestPing{})) << "the queue holds two waiting";
    const std::uint64_t refusedBefore = Events->RefusedCount();

    // The occurrence being delivered does not hold a waiting place, so each
    // reaction finds room although the queue was full when the drain began.
    EXPECT_EQ(Events->Drain(1).Delivered, 4u);
    EXPECT_EQ(echoer.Accepted, 2);
    EXPECT_EQ(after.Heard.size(), 2u);
    EXPECT_EQ(Events->RefusedCount(), refusedBefore);
}

// Payloads a declaration refuses: a choice it does not list, a number that is
// not finite. Encoders written by hand here, because the generated ones only
// ever encode what their C++ type holds.
struct TestMood
{
    std::string Choice;
};
template<>
struct AuthoredApiDefinition<TestMood>
{
    static constexpr std::string_view EventName = "test.mood";
    static void Encode(const TestMood& event, AuthoredArguments& payload)
    {
        payload.Resize(1);
        payload.Set(0, AuthoredValue::Enum(event.Choice));
    }
};

struct TestRatio
{
    double Value = 0.0;
};
template<>
struct AuthoredApiDefinition<TestRatio>
{
    static constexpr std::string_view EventName = "test.ratio";
    static void Encode(const TestRatio& event, AuthoredArguments& payload)
    {
        payload.Resize(1);
        payload.Set(0, AuthoredValue::Float(event.Value));
    }
};

TEST_F(AuthoredEventDispatchTest, APayloadTheDeclarationDoesNotAllowIsRefusedAtPublish)
{
    {
        AuthoredEventDefinition mood;
        mood.Name = "test.mood";
        DataFieldSchema choice;
        choice.Key = "choice";
        choice.Kind = DataFieldKind::Enum;
        choice.EnumChoices = { DataEnumChoice{ .Value = "calm", .DisplayName = {}, .Description = {} } };
        mood.Payload.Children.push_back(std::move(choice));
        AuthoredEventDefinition ratio;
        ratio.Name = "test.ratio";
        DataFieldSchema value;
        value.Key = "value";
        value.Kind = DataFieldKind::Float;
        ratio.Payload.Children.push_back(std::move(value));
        AuthoredEventRegistrationScope scope(Registry, "test");
        (void)scope.Declare(std::move(mood));
        (void)scope.Declare(std::move(ratio));
        ASSERT_TRUE(scope.Commit());
    }

    EXPECT_FALSE(Events->Publish(EntityId{}, TestMood{ .Choice = "sulky" }));
    EXPECT_FALSE(Events->Publish(EntityId{}, TestRatio{ .Value = std::numeric_limits<double>::quiet_NaN() }));
    EXPECT_EQ(Events->Queued(), 0u);
    EXPECT_EQ(Events->RefusedCount(), 2u);

    EXPECT_TRUE(Events->Publish(EntityId{}, TestMood{ .Choice = "calm" }));
    EXPECT_TRUE(Events->Publish(EntityId{}, TestRatio{ .Value = 0.5 }));
    EXPECT_EQ(Events->Queued(), 2u);
}

TEST_F(AuthoredEventDispatchTest, ASubscriberHearsOnlyTheContractItResolved)
{
    Listener old;
    AuthoredEventSubscription oldSubscription =
        Events->Subscribe<&Listener::Deliver>(PingEvent, EntityId{}, old);

    // The payload's contract changes: the count gains a range.
    {
        AuthoredEventDefinition ping;
        ping.Name = "test.ping";
        DataFieldSchema count;
        count.Key = "count";
        count.Kind = DataFieldKind::Int;
        count.Numeric.Maximum = 100.0;
        ping.Payload.Children.push_back(std::move(count));
        AuthoredEventRegistrationScope scope(Registry, "test");
        (void)scope.Declare(std::move(ping));
        ASSERT_TRUE(scope.Commit());
    }
    ASSERT_FALSE(Registry.IsCurrent(PingEvent));

    // The stale handle subscribes nothing; one resolved again does.
    Listener stale;
    EXPECT_FALSE(Events->Subscribe<&Listener::Deliver>(PingEvent, EntityId{}, stale).IsValid());
    Listener current;
    AuthoredEventSubscription currentSubscription =
        Events->Subscribe<&Listener::Deliver>(Registry.Resolve("test.ping"), EntityId{}, current);

    (void)Events->Publish(EntityId{}, TestPing{ .Count = 5 });
    (void)Events->Drain(1);
    EXPECT_TRUE(old.Heard.empty()) << "a subscriber was handed a payload layout it was not written for";
    EXPECT_EQ(current.Heard.size(), 1u);
}

TEST_F(AuthoredEventDispatchTest, AHandleFromAnotherCatalogSubscribesNothing)
{
    AuthoredEventRegistry elsewhere;
    AuthoredEventRegistrationScope scope(elsewhere, "elsewhere");
    (void)scope.Declare(Event("elsewhere.first"));
    (void)scope.Declare(Event("elsewhere.second"));
    ASSERT_TRUE(scope.Commit());
    const AuthoredEventHandle foreign = elsewhere.Resolve("elsewhere.second");
    ASSERT_EQ(foreign.Slot, PingEvent.Slot);

    Listener listener;
    EXPECT_FALSE(Events->Subscribe<&Listener::Deliver>(foreign, EntityId{}, listener).IsValid());
}

TEST_F(AuthoredEventDispatchTest, ReleasingManySubscriptionsKeepsTheRestInOrder)
{
    std::vector<int> order;
    std::vector<Listener> listeners(200);
    std::vector<AuthoredEventSubscription> subscriptions;
    for (int index = 0; index < 200; ++index)
    {
        listeners[static_cast<std::size_t>(index)].Tag = index;
        listeners[static_cast<std::size_t>(index)].Order = &order;
        subscriptions.push_back(Events->Subscribe<&Listener::Deliver>(
            OtherEvent, EntityId{}, listeners[static_cast<std::size_t>(index)]));
    }
    for (std::size_t index = 0; index < subscriptions.size(); index += 2)
        subscriptions[index].Reset();
    EXPECT_EQ(Events->SubscriptionCount(), 100u);

    (void)Events->Publish(EntityId{}, TestOther{});
    (void)Events->Drain(1);
    ASSERT_EQ(order.size(), 100u);
    for (std::size_t index = 0; index < order.size(); ++index)
        EXPECT_EQ(order[index], static_cast<int>(index * 2 + 1));
}

namespace
{
struct Thrower
{
    bool Throw = true;
    int Heard = 0;

    static void Deliver(Thrower& self, const AuthoredEventDelivery&)
    {
        ++self.Heard;
        if (self.Throw)
            throw std::runtime_error("the subscriber failed");
    }
};
} // namespace

TEST_F(AuthoredEventDispatchTest, ASubscriberThatThrowsLeavesTheDispatcherUsable)
{
    Thrower thrower;
    AuthoredEventSubscription subscription =
        Events->Subscribe<&Thrower::Deliver>(OtherEvent, EntityId{}, thrower);
    (void)Events->Publish(EntityId{}, TestOther{});

    EXPECT_THROW((void)Events->Drain(1), std::runtime_error);
    EXPECT_FALSE(Events->IsDraining());

    // The occurrence it was delivering is still at the head, and the next
    // drain delivers it.
    thrower.Throw = false;
    EXPECT_EQ(Events->Drain(2).Delivered, 1u);
    EXPECT_EQ(thrower.Heard, 2);
}
