#include <logic/decision/DecisionProfile.h>

#include <core/metadata/DataSchemaFields.h>

#include <algorithm>
#include <utility>

namespace
{
    DataFieldSchema Member(DataFieldKind kind, std::string key, std::string display, std::string summary,
                           bool required = false)
    {
        return DataField::Field(std::move(key), kind, std::move(display), std::move(summary), required);
    }

    DataFieldSchema Positive(std::string key, std::string display, double fallback, std::string summary)
    {
        DataFieldSchema field = Member(DataFieldKind::Float, std::move(key), std::move(display), std::move(summary));
        field.Numeric.Minimum = 0.0;
        field.Default = fallback;
        return field;
    }

    DataFieldSchema DataRef(std::string key, std::string display, std::string_view subtype, std::string summary,
                            bool required = false)
    {
        return DataField::DataRef(std::move(key), std::move(display), std::move(summary), subtype, required);
    }

    bool Flag(const JsonValue& record, std::string_view key)
    {
        const JsonValue* value = record.Find(key);
        return value != nullptr && value->AsBool();
    }

    float NumberOr(const JsonValue& record, std::string_view key, float fallback)
    {
        return static_cast<float>(record.NumberOr(key, fallback));
    }

    std::string ReadPlan(const JsonValue& authored, DecisionOptionPlan& plan, std::vector<AssetRef>& dependencies)
    {
        plan.Option = authored.Find("option")->AsString();
        plan.Ability = authored.Find("ability")->AsString();
        if (const JsonValue* position = authored.Find("position"))
        {
            plan.Position = position->AsString();
            dependencies.push_back(AssetRef{ AssetType::Data, plan.Position });
        }
        plan.ToLastSeen = Flag(authored, "to_last_seen");
        if (plan.ToLastSeen && !plan.Position.empty())
            return "option '" + plan.Option + "' goes both to a chosen place and to the last seen one";
        plan.RepositionDistance = NumberOr(authored, "reposition_distance", plan.RepositionDistance);
        plan.PositionSeconds = NumberOr(authored, "position_seconds", plan.PositionSeconds);
        plan.ReplaceOnArrival = Flag(authored, "replace_on_arrival");
        if (const JsonValue* claim = authored.Find("claim"))
        {
            plan.Claim = claim->Find("tag")->AsString();
            plan.ClaimCount = static_cast<std::uint32_t>(NumberOr(*claim, "count", 1.0f));
            plan.ClaimShare = NumberOr(*claim, "share", 0.0f);
        }
        plan.MarksVisited = Flag(authored, "marks_visited");
        plan.Commits = Flag(authored, "commits");
        dependencies.push_back(AssetRef{ AssetType::Data, plan.Ability });
        return {};
    }

    DataFieldSchema MakePlanRecord(std::vector<DataFieldSchema> extra)
    {
        DataFieldSchema plan = DataField::Field("plan", DataFieldKind::Record, "Plan", {});
        plan.Children.push_back(Member(DataFieldKind::String, "option", "Option", "The table option this plan carries out.", true));
        plan.Children.push_back(DataRef("ability", "Ability", "ability.definition", "The ability the option runs.", true));
        plan.Children.push_back(DataRef("position", "Position", "candidates.evaluation", "Chooses where the option goes."));
        plan.Children.push_back(Member(DataFieldKind::Bool, "to_last_seen", "To last seen",
                                       "Goes where the target was last known to be."));
        plan.Children.push_back(Positive("reposition_distance", "Reposition distance", 4.0,
                                         "Metres the target may move before the place is chosen again."));
        plan.Children.push_back(Positive("position_seconds", "Position seconds", 8.0,
                                         "Seconds before the place is chosen again regardless."));
        plan.Children.push_back(Member(DataFieldKind::Bool, "replace_on_arrival", "Replace on arrival",
                                       "A place reached is spent: the option chooses its next one."));
        DataFieldSchema claim = Member(DataFieldKind::Record, "claim", "Claim",
                                       "A place the option needs in a claim within the agent's group.");
        claim.Children.push_back(Member(DataFieldKind::GameplayTag, "tag", "Claim", "Which claim.", true));
        claim.Children.push_back(Positive("count", "Count", 1.0, "Places the claim holds."));
        claim.Children.push_back(Positive("share", "Share", 0.0, "Places as a share of the group, when that is more."));
        plan.Children.push_back(std::move(claim));
        plan.Children.push_back(Member(DataFieldKind::Bool, "marks_visited", "Marks visited",
                                       "What the option's place reaches is recorded for the group as visited."));
        plan.Children.push_back(Member(DataFieldKind::Bool, "commits", "Commits",
                                       "Once begun, nothing else is considered until its ability ends."));
        for (DataFieldSchema& field : extra)
            plan.Children.push_back(std::move(field));
        return plan;
    }
}

const DecisionOptionPlan* DecisionProfile::PlanFor(std::string_view option) const
{
    const auto found = std::ranges::find(Options, option, &DecisionOptionPlan::Option);
    return found != Options.end() ? &*found : nullptr;
}

std::string ReadDecisionProfile(const JsonValue& data, DecisionProfile& out, std::vector<AssetRef>& dependencies)
{
    out.Hostile = data.Find("hostile")->AsString();
    out.Table = data.Find("table")->AsString();
    out.DecisionSeconds = NumberOr(data, "decision_seconds", out.DecisionSeconds);
    out.UnavailableSeconds = NumberOr(data, "unavailable_seconds", out.UnavailableSeconds);
    dependencies.push_back(AssetRef{ AssetType::Data, out.Table });
    const JsonValue* options = data.Find("options");
    if (options == nullptr)
        return {};
    for (const JsonValue& authored : options->AsArray())
    {
        DecisionOptionPlan plan;
        if (std::string problem = ReadPlan(authored, plan, dependencies); !problem.empty())
            return problem;
        if (out.PlanFor(plan.Option) != nullptr)
            return "option '" + plan.Option + "' has two plans";
        out.Options.push_back(std::move(plan));
    }
    return {};
}

std::vector<DataFieldSchema> MakeDecisionProfileFields(std::vector<DataFieldSchema> optionMembers)
{
    std::vector<DataFieldSchema> fields;
    fields.push_back(Member(DataFieldKind::GameplayTag, "hostile", "Hostile", "Subjects carrying this tag are targets.", true));
    fields.push_back(DataRef("table", "Table", "selection.table", "Where the options come from.", true));
    fields.push_back(Positive("decision_seconds", "Decision interval", 0.25, "Seconds between decisions."));
    fields.push_back(Positive("unavailable_seconds", "Unavailable seconds", 1.0,
                              "Seconds an option with nowhere to go stays ruled out."));
    DataFieldSchema plans = Member(DataFieldKind::Array, "options", "Options", {});
    plans.Editor.Widget = "cards";
    plans.Children.push_back(MakePlanRecord(std::move(optionMembers)));
    fields.push_back(std::move(plans));
    return fields;
}
