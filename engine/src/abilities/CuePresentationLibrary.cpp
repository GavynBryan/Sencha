#include <abilities/CuePresentationLibrary.h>

#include <abilities/AbilityCapacity.h>

#include <authored/VerbBindingCompiler.h>
#include <authored/WorldVocabulary.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <format>

namespace
{
bool Compile(const VerbBindingDesc& desc, VerbKind kind, const VerbBindingEnvironment& environment,
             const VerbRegistry& verbs, CompiledVerbBinding& out, std::vector<std::string>& errors)
{
    std::vector<std::string> problems;
    if (!CompileVerbBinding(desc, environment, out, problems))
    {
        for (std::string& problem : problems)
            errors.push_back(std::format("{}: {}", desc.Key, problem));
        return false;
    }
    const VerbDefinition* definition = verbs.Get(out.Verb);
    if (definition == nullptr || definition->Scope != AuthoredScope::Cosmetic)
    {
        errors.push_back(std::format("{}: '{}' is a gameplay operation, which a presentation never runs (scope_mismatch)",
                                     desc.Key, desc.VerbName));
        return false;
    }
    if (definition->Kind != kind)
    {
        errors.push_back(std::format("{}: '{}' is {}", desc.Key, desc.VerbName,
                                     kind == VerbKind::Lease ? "not a lease, which a sustained presentation holds"
                                                             : "a lease, which a single occurrence cannot hold"));
        return false;
    }
    return true;
}
} // namespace

void CuePresentationLibrary::Declare(std::string_view path, const CuePresentationDeclaration& declaration,
                                     const World& world, std::vector<std::string>& errors)
{
    Declare(path, declaration, MakeVerbBindingEnvironment(world), errors);
}

void CuePresentationLibrary::Declare(std::string_view path, const CuePresentationDeclaration& declaration,
                                     const VerbBindingEnvironment& environment, std::vector<std::string>& errors)
{
    const VerbRegistry* verbs = environment.Verbs;
    const GameplayTagRegistry* tags = environment.Tags;
    std::vector<std::pair<GameplayTagId, Presentation>> compiled;
    if (verbs != nullptr && tags != nullptr)
    {
        for (const CuePresentationEntry& entry : declaration.Entries)
        {
            const GameplayTagId cue = tags->FindTag(entry.Cue);
            if (!cue.IsValid())
            {
                errors.push_back(std::format("{} {}: no gameplay tag '{}'", path, entry.Key, entry.Cue));
                continue;
            }
            Presentation presentation;
            for (const VerbBindingDesc& desc : entry.Once)
                if (CompiledVerbBinding binding; Compile(desc, VerbKind::Instant, environment, *verbs, binding, errors))
                    presentation.Once.push_back(std::move(binding));
            for (const VerbBindingDesc& desc : entry.Sustain)
                if (CompiledVerbBinding binding; Compile(desc, VerbKind::Lease, environment, *verbs, binding, errors))
                    presentation.Sustain.push_back(std::move(binding));
            compiled.emplace_back(cue, std::move(presentation));
        }
    }
    std::vector<GameplayTagId> cues;
    for (const auto& [cue, presentation] : compiled)
        cues.push_back(cue);
    ByPath[std::string(path)] = std::move(compiled);
    Rebuild();
    for (const GameplayTagId cue : cues)
        if (const Presentation* merged = Find(cue); merged != nullptr && merged->Sustain.size() > kAbilityMaxSustainedPresentations)
            errors.push_back(std::format("{}: a cue sustains at most {} presentations across every declaration; the "
                                         "ones past it are never held",
                                         path, kAbilityMaxSustainedPresentations));
}

void CuePresentationLibrary::Undeclare(std::string_view path)
{
    if (const auto found = ByPath.find(path); found != ByPath.end())
    {
        ByPath.erase(found);
        Rebuild();
    }
}

const CuePresentationLibrary::Presentation* CuePresentationLibrary::Find(GameplayTagId cue) const
{
    const auto found = ByCue.find(cue.Value);
    return found != ByCue.end() ? &found->second : nullptr;
}

void CuePresentationLibrary::Rebuild()
{
    ByCue.clear();
    for (const auto& [path, presentations] : ByPath)
        for (const auto& [cue, presentation] : presentations)
        {
            Presentation& merged = ByCue[cue.Value];
            merged.Once.insert(merged.Once.end(), presentation.Once.begin(), presentation.Once.end());
            merged.Sustain.insert(merged.Sustain.end(), presentation.Sustain.begin(), presentation.Sustain.end());
        }
}
