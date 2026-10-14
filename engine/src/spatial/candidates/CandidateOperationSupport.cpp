#include "CandidateOperationSupport.h"

#include <gameplay_tags/GameplayTagRegistry.h>
#include <navigation/NavigationQuery.h>

#include <format>
#include <utility>

namespace candidate_operation
{
    DataFieldSchema SlotField(std::string key, std::string displayName, std::string fallback)
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::String, std::move(key), std::move(displayName),
                                              "A slot the evaluation declares, or \"querier\".");
        if (!fallback.empty())
        {
            field.Required = false;
            field.Default = std::move(fallback);
        }
        return field;
    }

    DataFieldSchema FloatField(std::string key, std::string displayName, std::optional<double> fallback,
                               std::optional<double> minimum)
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::Float, std::move(key), std::move(displayName));
        field.Numeric.Minimum = minimum;
        if (fallback)
        {
            field.Required = false;
            field.Default = *fallback;
        }
        return field;
    }

    DataFieldSchema IntField(std::string key, std::string displayName, std::int64_t fallback, std::int64_t minimum)
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::Int, std::move(key), std::move(displayName));
        field.Numeric.Minimum = static_cast<double>(minimum);
        field.Required = false;
        field.Default = fallback;
        return field;
    }

    DataFieldSchema BoolField(std::string key, std::string displayName, bool fallback)
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::Bool, std::move(key), std::move(displayName));
        field.Required = false;
        field.Default = fallback;
        return field;
    }

    DataFieldSchema EnumField(std::string key, std::string displayName,
                              std::initializer_list<std::string_view> choices, std::string_view fallback)
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::Enum, std::move(key), std::move(displayName));
        for (const std::string_view choice : choices)
            field.EnumChoices.push_back(DataEnumChoice{ std::string(choice), std::string(choice), {} });
        if (!fallback.empty())
        {
            field.Required = false;
            field.Default = std::string(fallback);
        }
        return field;
    }

    DataFieldSchema Record(std::initializer_list<DataFieldSchema> fields)
    {
        DataFieldSchema record = AuthoredRecordRoot();
        record.Children.assign(fields.begin(), fields.end());
        return record;
    }

    std::string_view StringOr(const JsonValue& arguments, std::string_view key, std::string_view fallback)
    {
        const JsonValue* value = arguments.Find(key);
        return value != nullptr && value->IsString() ? std::string_view(value->AsString()) : fallback;
    }

    bool BoolOr(const JsonValue& arguments, std::string_view key, bool fallback)
    {
        const JsonValue* value = arguments.Find(key);
        return value != nullptr && value->IsBool() ? value->AsBool() : fallback;
    }

    float FloatOr(const JsonValue& arguments, std::string_view key, float fallback)
    {
        return static_cast<float>(arguments.NumberOr(key, fallback));
    }

    std::optional<std::uint8_t> ReadSlot(const JsonValue& arguments,
                                         std::string_view key,
                                         std::string_view fallback,
                                         const CandidatePrepareContext& context,
                                         std::string_view subject,
                                         std::vector<std::string>& errors)
    {
        const std::string_view name = StringOr(arguments, key, fallback);
        const std::optional<std::uint8_t> slot = context.FindSlot(name);
        if (!slot)
            errors.push_back(std::format("{}: '{}' names no slot this evaluation declares", subject, name));
        return slot;
    }

    DataFieldSchema ReduceField()
    {
        return EnumField("reduce", "Reduce", { "min", "max" }, "min");
    }

    Reduce ReadReduce(const JsonValue& arguments)
    {
        return StringOr(arguments, "reduce", "min") == "max" ? Reduce::Max : Reduce::Min;
    }

    DataFieldSchema TagQueryField(std::string key, std::string displayName)
    {
        const auto tagList = [](std::string listKey, std::string listName) {
            DataFieldSchema list = MakeDataField(DataFieldKind::Array, std::move(listKey), std::move(listName));
            list.Required = false;
            list.Children.push_back(MakeDataField(DataFieldKind::GameplayTag, "tag", "Tag"));
            return list;
        };
        DataFieldSchema query = MakeDataField(DataFieldKind::Record, std::move(key), std::move(displayName));
        query.Required = false;
        query.Children.push_back(tagList("all", "All of"));
        query.Children.push_back(tagList("any", "Any of"));
        query.Children.push_back(tagList("none", "None of"));
        query.Children.push_back(EnumField("match", "Match", { "exact", "hierarchical" }, "exact"));
        return query;
    }

    TagQueryNames ReadTagQuery(const JsonValue& arguments, std::string_view key)
    {
        TagQueryNames names;
        const JsonValue* query = arguments.Find(key);
        if (query == nullptr)
            return names;
        const auto read = [query](std::string_view listKey, std::vector<std::string>& out) {
            if (const JsonValue* list = query->Find(listKey))
            {
                for (const JsonValue& tag : list->AsArray())
                    out.push_back(tag.AsString());
            }
        };
        read("all", names.All);
        read("any", names.Any);
        read("none", names.None);
        if (StringOr(*query, "match", "exact") == "hierarchical")
            names.Match = GameplayTagMatchMode::Hierarchical;
        return names;
    }

    bool BindTagQuery(const TagQueryNames& names,
                      const CandidateBindEnvironment& environment,
                      std::string_view subject,
                      std::vector<std::string>& errors,
                      GameplayTagQuery& out)
    {
        if (names.Empty())
            return true;
        if (environment.Tags == nullptr)
        {
            errors.push_back(std::format("{}: no tag registry to resolve tags against", subject));
            return false;
        }
        bool ok = true;
        const auto add = [&](const std::vector<std::string>& list, auto addClause) {
            for (const std::string& name : list)
            {
                const GameplayTagId tag = environment.Tags->FindTag(name);
                if (!tag.IsValid())
                {
                    errors.push_back(std::format("{}: '{}' is not a registered gameplay tag", subject, name));
                    ok = false;
                    continue;
                }
                (out.*addClause)(tag, names.Match);
            }
        };
        add(names.All, &GameplayTagQuery::AddAll);
        add(names.Any, &GameplayTagQuery::AddAny);
        add(names.None, &GameplayTagQuery::AddNone);
        return ok;
    }

    std::vector<DataFieldSchema> ProjectionFields()
    {
        return { BoolField("project", "Project onto navigation", true),
                 FloatField("project_height", "Projection height", 2.0, 0.0),
                 BoolField("keep_unprojected", "Keep unprojected points", false) };
    }

    Projection ReadProjection(const JsonValue& arguments)
    {
        return Projection{ BoolOr(arguments, "project", true), FloatOr(arguments, "project_height", 2.0f),
                           BoolOr(arguments, "keep_unprojected", false) };
    }

    bool AppendSample(CandidateRun& run, const Vec3d& point, const Projection& projection)
    {
        const ZoneNavigation* navigation = run.Navigation();
        if (!projection.Project || navigation == nullptr)
            return run.Append(point);

        const Vec3d extents(0.5f, projection.Height, 0.5f);
        const NavProjectResult projected =
            NavProjectPoint(*navigation, run.NavContext(), run.NavRequest(), point, extents);
        if (projected.Status == NavStatus::Success)
            return run.Append(projected.Location.Position, {}, projected.Location);
        if (projection.KeepUnprojected)
            return run.Append(point);
        run.CountDroppedAtGeneration();
        return !run.Full();
    }

    DataFieldSchema OutsideZoneField()
    {
        return EnumField("outside_zone", "Outside the querier's zone", { "reject", "estimate" }, "reject");
    }

    OutsideZone ReadOutsideZone(const JsonValue& arguments)
    {
        return StringOr(arguments, "outside_zone", "reject") == "estimate" ? OutsideZone::Estimate
                                                                          : OutsideZone::Reject;
    }
}
