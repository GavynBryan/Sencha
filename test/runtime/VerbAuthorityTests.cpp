#include <app/EngineVocabulary.h>
#include <authored/VerbBinding.h>
#include <authored/VerbDispatcher.h>
#include <authored/VerbRegistry.h>
#include <authored/WorldVocabulary.h>
#include <ecs/World.h>
#include <world/SimulationAuthority.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
struct ClientWorld
{
    ClientWorld()
    {
        InstallAuthoredVocabulary(Entities);
        EXPECT_TRUE(InstallEngineVocabulary(Entities));
        InstallEveryKitVocabulary(Entities);
        Entities.AddResource<SimulationAuthority>().Authoritative = false;
        Verbs = FindVerbRegistry(Entities);
    }

    CompiledVerbBinding BindingOf(VerbId verb) const
    {
        CompiledVerbBinding binding;
        binding.Catalog = Verbs->Catalog();
        binding.Verb = verb;
        binding.Revision = Verbs->Revision(verb);
        binding.KeyText = "authority.test";
        binding.Key = MakeVerbBindingKey(binding.KeyText);
        binding.Constants.Resize(Verbs->Get(verb)->Arguments.Children.size());
        return binding;
    }

    VerbAdmission Call(VerbDispatcher& dispatcher, VerbId verb) const
    {
        const CompiledVerbBinding binding = BindingOf(verb);
        if (Verbs->Get(verb)->Kind == VerbKind::Lease)
            return dispatcher.AcquireLease(binding, {}, LeaseOwner{ .Entity = Entities.CreateEntity(), .Key = 1 }).Status;
        return dispatcher.Invoke(binding, {}).Status;
    }

    mutable World Entities;
    VerbRegistry* Verbs = nullptr;
};
}

TEST(VerbAuthority, EveryAuthorityOnlyEngineVerbAnswersNotAuthorityOnAClient)
{
    ClientWorld client;
    VerbDispatcher dispatcher(*client.Verbs);
    dispatcher.SetAuthority(&client.Entities);
    std::size_t asked = 0;
    for (const VerbId verb : client.Verbs->Live())
    {
        const VerbDefinition& definition = *client.Verbs->Get(verb);
        if (definition.Prediction != AuthoredPrediction::AuthorityOnly)
            continue;
        ++asked;
        EXPECT_EQ(client.Call(dispatcher, verb), VerbAdmission::NotAuthority) << definition.Name;
    }
    EXPECT_GT(asked, 30u);
}

TEST(VerbAuthority, APredictedOrCosmeticVerbIsNotRefusedForAuthority)
{
    ClientWorld client;
    VerbDispatcher dispatcher(*client.Verbs);
    dispatcher.SetAuthority(&client.Entities);
    for (const VerbId verb : client.Verbs->Live())
    {
        const VerbDefinition& definition = *client.Verbs->Get(verb);
        if (definition.Prediction == AuthoredPrediction::AuthorityOnly)
            continue;
        EXPECT_NE(client.Call(dispatcher, verb), VerbAdmission::NotAuthority) << definition.Name;
    }
}

TEST(VerbAuthority, TheAuthorityAndAnUnsetHostAreNotRefused)
{
    ClientWorld world;
    world.Entities.GetResource<SimulationAuthority>().Authoritative = true;
    VerbDispatcher authority(*world.Verbs);
    authority.SetAuthority(&world.Entities);
    const VerbId give = world.Verbs->Find("inventory.give");
    EXPECT_EQ(world.Call(authority, give), VerbAdmission::Unavailable) << "nothing bound, and it is the authority";

    world.Entities.GetResource<SimulationAuthority>().Authoritative = false;
    VerbDispatcher unset(*world.Verbs);
    EXPECT_EQ(world.Call(unset, give), VerbAdmission::Unavailable) << "a dispatcher no host told about authority";
}
