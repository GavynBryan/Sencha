#include "authoring/AnimationContentTags.h"

#include "data/DataDocumentSet.h"

#include <assets/runtime/ContentTagDeclarations.h>
#include <gameplay_tags/GameplayTagDeclarations.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <ecs/World.h>

#include <algorithm>

void AnimationContentTags::Refresh(RuntimeAssets& assets, const DataDocumentSet& documents)
{
    Declared.clear();
    Problems.clear();
    CollectContentTags(assets, Declared, Problems);
    // A name removed in an open document stays declared until that document is saved.
    for (const auto& document : documents.Documents())
    {
        const JsonValue* data = document->Subtype() == kGameplayTagDeclarationsType ? document->Data() : nullptr;
        const JsonValue* tags = data != nullptr ? data->Find("tags") : nullptr;
        if (tags == nullptr || !tags->IsArray())
            continue;
        for (const JsonValue& tag : tags->AsArray())
            if (tag.IsString() && std::ranges::find(Declared, tag.AsString()) == Declared.end())
                Declared.push_back(tag.AsString());
    }
}

std::function<void(World&)> AnimationContentTags::Vocabulary(std::function<void(World&)> module) const
{
    return [module = std::move(module), this](World& world) {
        if (module)
            module(world);
        if (GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>())
            for (const std::string& name : Declared)
                (void)tags->RegisterTag(name);
    };
}
