// Authored content asks for animation through anim.request and anim.cancel: the
// same door an ability uses, with the invocation's attribution carried through to
// what the request plays.

#include "AnimFlowFixture.h"

#include <anim/AnimEventSystem.h>
#include <anim/AnimRequestVerbs.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/VerbDispatcher.h>
#include <authored/VerbRegistry.h>
#include <authored/WorldVocabulary.h>
#include <time/SimClock.h>

#include <gtest/gtest.h>

namespace
{
    struct MarkRecorder
    {
        std::vector<VerbInvocation> Calls;
        VerbAdmission Invoke(const VerbInvocation& invocation)
        {
            VerbInvocation copy = invocation;
            copy.Arguments = nullptr;
            Calls.push_back(copy);
            return VerbAdmission::Accepted;
        }
    };

    VerbBindingArgument FromInput(std::string key)
    {
        VerbBindingArgument argument;
        argument.Key = key;
        argument.Source = VerbArgumentSource::Input;
        argument.Text = std::move(key);
        return argument;
    }

    VerbBindingArgument Constant(std::string key, JsonValue value)
    {
        VerbBindingArgument argument;
        argument.Key = std::move(key);
        argument.Literal = std::move(value);
        return argument;
    }

    VerbBindingArgument TagArgument(std::string key, std::string tag)
    {
        VerbBindingArgument argument;
        argument.Key = std::move(key);
        argument.Source = VerbArgumentSource::Tag;
        argument.Text = std::move(tag);
        return argument;
    }

    // The reload rig, whose open section marks its midpoint, on a prop; the anim
    // verbs bound behind a dispatcher, and a recorder behind the mark.
    struct VerbFixture : AnimRigFixture
    {
        DataAssetHandle Rig;
        EntityId Prop;
        EntityId Player;
        std::optional<VerbDispatcher> Dispatcher;
        FixedSimulationLoop Clock;
        std::optional<AnimRequestOperations> Operations;
        MarkRecorder Marks;
        VerbBindingToken Tokens[3];
        std::optional<AnimEventSystem> Events;

        VerbFixture()
        {
            {
                VerbRegistrationScope scope(Verbs(), "engine");
                DeclareAnimRequestVerbs(scope);
                VerbDefinition mark;
                mark.Name = "test.mark";
                (void)scope.Declare(std::move(mark));
                EXPECT_TRUE(scope.Commit());
            }
            AnimationClipEvent midpoint;
            midpoint.Key = 1;
            midpoint.Time = 0.5f;
            midpoint.Binding = "anim.mark";
            Clip("asset://anim/open.sanim", 0.25f, { midpoint });
            (void)Load("asset://anim/v.bindings.sdata", kVerbBindingsTypeName,
                       R"({ "bindings": [ { "key": "anim.mark", "verb": "test.mark", "inputs": [] } ] })");
            Rig = LoadReloadRig(*this, CountedLoopFlow(), "cancel_section", {}, {}, "asset://anim/v.bindings.sdata");
            Prop = Character(Rig);
            Player = Entities.CreateEntity();

            Dispatcher.emplace(Verbs());
            Operations.emplace(Entities, Clock);
            Tokens[0] = Dispatcher->Bind<&InvokeAnimRequest>(Verbs().Find(kAnimRequestVerb), *Operations);
            Tokens[1] = Dispatcher->Bind<&InvokeAnimCancel>(Verbs().Find(kAnimCancelVerb), *Operations);
            Tokens[2] = Dispatcher->Bind(Verbs().Find("test.mark"), Marks);
            Events.emplace(&*Dispatcher, true);
        }

        VerbInvocationResult Invoke(std::string_view verb, std::vector<VerbBindingArgument> arguments)
        {
            VerbBindingDesc desc;
            desc.Key = std::string(verb);
            desc.KeyId = MakeVerbBindingKey(desc.Key);
            desc.VerbName = std::string(verb);
            desc.Inputs = { "target" };
            desc.Arguments = std::move(arguments);
            CompiledVerbBinding compiled;
            std::vector<std::string> errors;
            EXPECT_TRUE(CompileVerbBinding(desc, MakeVerbBindingEnvironment(Entities), compiled, errors))
                << (errors.empty() ? std::string() : errors.front());
            const AuthoredValue target = AuthoredValue::Entity(Prop);
            VerbInvocationSource source;
            source.Instigator = Player;
            source.Tick = Now;
            return Dispatcher->Invoke(compiled, std::span(&target, 1), source);
        }

        void Step(int ticks)
        {
            for (int i = 0; i < ticks; ++i)
            {
                Tick();
                Events->Run(Entities, Last(), kTick);
            }
        }
    };
}

TEST(AnimRequestVerbs, ARequestFromContentIsTheInstigatorsAndParentsWhatItPlays)
{
    VerbFixture fx;
    fx.Step(2);
    const VerbInvocationResult asked =
        fx.Invoke(kAnimRequestVerb, { FromInput("target"), TagArgument("intent", "anim.intent.reload"),
                                      Constant("lifetime", JsonValue("held")), Constant("ticks", JsonValue(0.0)) });
    ASSERT_TRUE(asked.Accepted()) << VerbAdmissionName(asked.Status);

    const AnimRequest* request = nullptr;
    for (const AnimRequest& record : static_cast<const World&>(fx.Entities).TryGet<AnimRequestSet>(fx.Prop)->Records)
        if (record.Occupied)
            request = &record;
    ASSERT_NE(request, nullptr);
    EXPECT_EQ(request->Id.Source, fx.Player);
    EXPECT_EQ(request->Lifetime, AnimRequestLifetime::Held);
    EXPECT_EQ(request->Cause, asked.Id);

    fx.Step(12);
    ASSERT_EQ(fx.Marks.Calls.size(), 1u) << "the open section's midpoint";
    EXPECT_EQ(fx.Marks.Calls[0].Producer, fx.Prop);
    EXPECT_EQ(fx.Marks.Calls[0].Instigator, fx.Player);
    EXPECT_EQ(fx.Marks.Calls[0].Parent, asked.Id);

    const VerbInvocationResult ended =
        fx.Invoke(kAnimCancelVerb, { FromInput("target"), TagArgument("intent", "anim.intent.reload") });
    ASSERT_TRUE(ended.Accepted()) << VerbAdmissionName(ended.Status);
    EXPECT_TRUE(request->IsCancelled());
    EXPECT_EQ(request->CancelReason, AnimCancelReason::Released);
}

// Nothing the target's rig does not declare is asked for, and a producer cannot
// end a request it did not make.
TEST(AnimRequestVerbs, ContentAsksOnlyForWhatTheRigDeclares)
{
    VerbFixture fx;
    (void)fx.Tags().RegisterTag("anim.intent.undeclared");
    fx.Step(1);
    EXPECT_EQ(fx.Invoke(kAnimRequestVerb, { FromInput("target"), TagArgument("intent", "anim.intent.undeclared"),
                                            Constant("lifetime", JsonValue("held")), Constant("ticks", JsonValue(0.0)) })
                  .Status,
              VerbAdmission::Refused);
    EXPECT_EQ(fx.Invoke(kAnimCancelVerb, { FromInput("target"), TagArgument("intent", "anim.intent.reload") }).Status,
              VerbAdmission::Refused);
}
