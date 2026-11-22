#include <app/LevelChangeVocabulary.h>

#include <app/LevelChange.h>
#include <authored/AuthoredValue.h>

#include <string>
#include <utility>

namespace
{
    DataFieldSchema Choice(std::string key, std::string label, std::string tooltip,
                           std::vector<DataEnumChoice> choices)
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::Enum, std::move(key), std::move(label), std::move(tooltip));
        field.EnumChoices = std::move(choices);
        return field;
    }
}

void DeclareLevelChangeVerbs(VerbRegistrationScope& scope)
{
    VerbDefinition change;
    change.Name = std::string(kLevelChangeVerb);
    change.DisplayName = "Change level";
    change.Description = "Replaces the loaded level with another, or the same one again, saying what it keeps.";
    change.Category = "Levels";
    change.Refusals = VerbRefusalsOf<LevelChangeAdmission>();
    DataFieldSchema kind = Choice("kind", "Kind", "A single scene, or a partitioned world.",
                                  { { "scene", "Scene", {} }, { "world", "World", {} } });
    kind.Required = false;
    kind.Default = std::string("scene");
    DataFieldSchema arrival = MakeDataField(DataFieldKind::String, "arrival", "Arrival",
                                            "The landmark bodies arrive at; the game's spawn when empty.");
    arrival.Required = false;
    arrival.Default = std::string();
    change.Arguments.Children = {
        MakeDataField(DataFieldKind::String, "level", "Level", "The level's name, as `map` or `world` takes it."),
        std::move(kind),
        Choice("memory", "Memory", "Remembered enters it as the game left it; authored as content made it.",
               { { "remembered", "Remembered", {} }, { "authored", "Authored", {} } }),
        Choice("bodies", "Bodies", "Kept carries the same bodies across; fresh spawns the game's prefab.",
               { { "kept", "Kept", {} }, { "fresh", "Fresh", {} } }),
        std::move(arrival),
    };
    (void)scope.Declare(std::move(change));
}

VerbAnswer InvokeLevelChange(LevelChange& changes, const VerbInvocation& invocation)
{
    std::string_view name;
    std::string_view kind;
    std::string_view memory;
    std::string_view bodies;
    std::string_view arrival;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetString(0, name) || name.empty())
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 0);
    if (!invocation.Arguments->TryGetEnum(1, kind))
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 1);
    if (!invocation.Arguments->TryGetEnum(2, memory))
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 2);
    if (!invocation.Arguments->TryGetEnum(3, bodies))
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 3);
    (void)invocation.Arguments->TryGetString(4, arrival);
    const LevelReference level{ .Kind = kind == "world" ? LevelKind::World : LevelKind::Scene, .Name = std::string(name) };
    LevelChangeRequest request = LevelChangeRequest::Travel(
        level, memory == "authored" ? LevelMemory::Authored : LevelMemory::Remembered,
        bodies == "kept" ? LevelChangeBodies::Kept : LevelChangeBodies::Fresh);
    request.At(std::string(arrival));
    return VerbAnswerOf(changes.Request(std::move(request)));
}
