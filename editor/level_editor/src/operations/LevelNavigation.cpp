#include "LevelNavigation.h"

#include "document/DocumentNavigation.h"
#include "document/EditorDocument.h"

#include <core/metadata/DataSchemaFields.h>

#include <algorithm>
#include <set>

namespace
{
    DataFieldSchema Names(std::string key, std::string summary)
    {
        return DataField::ArrayOf(std::move(key), {}, std::move(summary),
                                  DataField::Field({}, DataFieldKind::String, {}, {}));
    }

    std::vector<std::string> NamesFrom(const JsonValue* value)
    {
        std::vector<std::string> names;
        if (value != nullptr && value->IsArray())
            for (const JsonValue& name : value->AsArray())
                names.push_back(name.AsString());
        return names;
    }

    JsonValue NamesJson(const std::vector<std::string>& names)
    {
        JsonValue::Array array;
        for (const std::string& name : names)
            array.push_back(JsonValue(name));
        return JsonValue(std::move(array));
    }
}

NavQueryRequest LevelNavigation::Request(std::span<const GameplayTagId> capabilities) const
{
    NavQueryRequest request;
    request.Zone = Zone;
    request.Profile = Tags.FindTag(Profile);
    request.Capabilities = capabilities;
    return request;
}

JsonValue LevelNavigation::AssumptionsJson() const
{
    return JsonValue(JsonValue::Object{
        { "held", NamesJson(Held) },
        { "denied", NamesJson(Denied) },
        { "unknown", NamesJson(Unknown) },
    });
}

std::vector<DataFieldSchema> ScenarioParameters()
{
    return {
        Names("held", "traversal kinds the player can perform, e.g. navigation.traversal.jump"),
        Names("denied", "traversal kinds the player cannot perform"),
        DataField::Optional("profile", {}, "navigation profile; the first one when absent",
                            DataField::Field({}, DataFieldKind::String, {}, {})),
    };
}

DataFieldSchema AssumptionsSchema()
{
    return DataField::Record("assumptions", {}, "what the scenario took as given", {
        Names("held", {}), Names("denied", {}), Names("unknown", {}),
    });
}

std::unique_ptr<LevelNavigation> BuildLevelNavigation(const EditorDocument& document, const JsonValue& arguments,
                                                      EditorOperationOutcome& refusal)
{
    if (document.ContentRoots().empty())
    {
        refusal = EditorOperationOutcome::Refused(
            "nav.unavailable", "navigation needs a project: no content root holds navigation.settings");
        return nullptr;
    }
    DocumentNavigation built = BuildDocumentNavigation(document, document.ContentRoots().front());
    if (!built.File)
    {
        std::string why = "the project has no navigation.settings asset or the level has no walkable geometry";
        if (!built.Diagnostics.empty())
            why = built.Diagnostics.front().Rule + ": " + built.Diagnostics.front().Message;
        refusal = EditorOperationOutcome::Refused("nav.unavailable", why);
        return nullptr;
    }

    auto level = std::make_unique<LevelNavigation>();
    level->File = std::move(*built.File);
    const NavigationFile& file = level->File;
    GameplayTagRegistry& tags = level->Tags;

    // The analysis names its own vocabulary: every name the built file and
    // the scenario use, registered with their parents.
    std::set<std::string> kinds;
    for (const NavLinkRecord& link : file.Links)
        kinds.insert(link.Traversal);
    level->Held = NamesFrom(arguments.Find("held"));
    level->Denied = NamesFrom(arguments.Find("denied"));
    for (const NavProfileRecord& profile : file.Profiles)
        (void)tags.RegisterTag(profile.Name);
    for (const std::string& area : file.Areas)
        (void)tags.RegisterTag(area);
    for (const std::vector<std::string>* names : { &level->Held, &level->Denied })
        for (const std::string& name : *names)
            if (!tags.RegisterTag(name))
            {
                refusal = EditorOperationOutcome::Refused("scenario.malformed", "'" + name + "' is not a tag name");
                return nullptr;
            }
    for (const std::string& kind : kinds)
        (void)tags.RegisterTag(kind);

    const auto covers = [&tags](const std::vector<std::string>& by, const std::string& kind) {
        return std::ranges::any_of(by, [&](const std::string& name) {
            return tags.IsDescendantOf(tags.FindTag(kind), tags.FindTag(name));
        });
    };
    for (const std::string& name : level->Held)
        level->Sure.push_back(tags.FindTag(name));
    level->Possible = level->Sure;
    for (const std::string& kind : kinds)
    {
        const bool isHeld = covers(level->Held, kind);
        const bool isDenied = covers(level->Denied, kind);
        if (isHeld && isDenied)
        {
            refusal = EditorOperationOutcome::Refused("scenario.contradictory", "'" + kind + "' is both held and denied");
            return nullptr;
        }
        if (!isHeld && !isDenied)
        {
            level->Unknown.push_back(kind);
            level->Possible.push_back(tags.FindTag(kind));
        }
    }

    const JsonValue* profile = arguments.Find("profile");
    level->Profile = profile != nullptr && profile->IsString()
        ? profile->AsString()
        : (file.Profiles.empty() ? std::string{} : file.Profiles.front().Name);
    if (!tags.FindTag(level->Profile).IsValid())
    {
        refusal = EditorOperationOutcome::Refused("nav.profile_unknown",
                                                  "the navigation has no profile '" + level->Profile + "'");
        return nullptr;
    }
    if (!level->Navigation.Load(level->Zone, file, &tags))
    {
        refusal = EditorOperationOutcome::Refused("nav.unavailable", "the built navigation did not load");
        return nullptr;
    }
    level->ProfileIndex = level->Navigation.FindProfile(tags.FindTag(level->Profile)).value_or(0);
    return level;
}
