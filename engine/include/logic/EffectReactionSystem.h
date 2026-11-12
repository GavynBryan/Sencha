#pragma once

#include <assets/data/DataAssetHandle.h>
#include <authored/BindingDependencyStamp.h>
#include <logic/EffectReactionProfile.h>

#include <cstdint>
#include <string>
#include <vector>

class DataAssetCache;
class Logger;
class World;
struct FixedLogicContext;

// Answers each application in the World's EffectApplicationLog by its target's
// EffectReactions profile: a push through AddMotionImpulse, and a shake of the
// view the target owns. It owns the log: it adds it, reads it once a tick on the
// authority after every pass that applies effects, and clears it.
// docs/gameplay/reactions.md.
class EffectReactionSystem
{
public:
    struct Services
    {
        const DataAssetCache* DataAssets = nullptr;
        Logger* Log = nullptr;
    };

    explicit EffectReactionSystem(Services services);

    void FixedLogic(FixedLogicContext& ctx);

private:
    struct BoundProfile
    {
        DataAssetStamp Asset;
        TagVocabularyStamp Tags;
        std::vector<EffectReactionRule> Rules;
    };

    // The profile's rules in `world`, bound again when the profile reloads or a tag it names appears.
    const std::vector<EffectReactionRule>* RulesFor(const World& world, DataAssetHandle profile);
    void React(World& world, const FixedLogicContext& ctx);

    Services Use;
    const World* BoundWorld = nullptr;
    std::vector<BoundProfile> Profiles;
};
