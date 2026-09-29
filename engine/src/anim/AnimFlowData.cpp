#include <anim/AnimFlowData.h>

#include "AnimSchemaFields.h"

#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <format>
#include <memory>

namespace
{
    std::vector<DataEnumChoice> Choices(std::initializer_list<DataEnumChoice> choices) { return choices; }

    DataSchema MakeSchema()
    {
        using AnimSchema::ArrayOf;
        using AnimSchema::Enum;
        using AnimSchema::Field;
        using AnimSchema::Record;
        const auto optional = [](DataFieldSchema field) {
            field.Required = false;
            return field;
        };

        DataFieldSchema clip = Field("clip", DataFieldKind::AssetRef, "Clip", "What the section plays.", false);
        clip.Reference.AssetTypeFilter = AssetType::AnimationClip;

        DataFieldSchema branch = Record({}, "Branch", {},
            {
                Field("to", DataFieldKind::GameplayTag, "To", "A later section, by tag."),
                AnimPredicateSchema("when", "When", "Taken at the section's end when this holds."),
            });
        const auto lifecycle = [&](std::string key, std::string label, std::string summary) {
            return Record(std::move(key), std::move(label), std::move(summary),
                {
                    Field("binding", DataFieldKind::String, "Binding",
                          "The authored binding key; it receives the section's tag as its 'section' input."),
                    optional(Enum("scope", "Scope", "Cosmetic where a pose is presented, gameplay on the authority.",
                                  Choices({ { "cosmetic", "Cosmetic", {} }, { "gameplay", "Gameplay", {} } }))),
                },
                false);
        };

        DataFieldSchema section = Record({}, "Section", {},
            {
                Field("tag", DataFieldKind::GameplayTag, "Tag", "The section's identity, e.g. Anim.Reload.Insert."),
                std::move(clip),
                Field("slot", DataFieldKind::GameplayTag, "Slot",
                      "A behavior resolved through the rig's slot map when the section is entered.", false),
                optional(Enum("loop", "Loop", "Whether and how the section repeats.",
                              Choices({ { "once", "Once", {} },
                                        { "while", "While", "Repeats while a predicate holds at its end" },
                                        { "count", "Count", "Repeats as many times as a request parameter says" } }))),
                AnimPredicateSchema("while", "While", "The loop condition, read at the section's end."),
                Field("count_intent", DataFieldKind::GameplayTag, "Count intent",
                      "The request whose parameter gives the count.", false),
                Field("count_param", DataFieldKind::String, "Count parameter", "The parameter's name.", false),
                ArrayOf("branches", "Branches", "Forward jumps, tried in order at the section's end.",
                        std::move(branch)),
                Field("ends", DataFieldKind::Bool, "Ends the flow",
                      "When no branch is taken, the flow ends here instead of moving on.", false),
                optional(Enum("cancel_timing", "Cancel", "When a cancel takes this section to the cancel section.",
                              Choices({ { "at_section_end", "At section end", {} },
                                        { "immediate", "Immediately", {} } }))),
            });
        DataFieldSchema sections = ArrayOf("sections", "Sections", "Played in order.", std::move(section), true);
        sections.Editor.Widget = "cards";
        sections.Editor.TitleKey = "tag";

        DataSchema schema;
        schema.TypeName = std::string(kAnimFlowType);
        schema.DisplayName = "Animation flow";
        schema.Description = "A forward-only sequence of sections with one cancel section.";
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children = {
            std::move(sections),
            Field("cancel", DataFieldKind::GameplayTag, "Cancel section",
                  "Where a cancel goes. None ends the flow.", false),
            lifecycle("on_section_entered", "On section entered", "Invoked as each section is entered."),
            lifecycle("on_section_exited", "On section exited", "Invoked as each section is left."),
        };
        return schema;
    }

    std::string Text(const JsonValue* object, std::string_view key)
    {
        const JsonValue* value = object != nullptr ? object->Find(key) : nullptr;
        return value != nullptr && value->IsString() ? value->AsString() : std::string();
    }

    DataAssetCompileResult Compile(const JsonValue& data)
    {
        DataAssetCompileResult result;
        auto flow = std::make_shared<AnimFlowData>();
        GameplayTagRegistry tagSyntax;
        const auto fail = [&](std::string at, std::string message) {
            result.Error = at + " " + message;
            return result;
        };
        const auto validTag = [&](const std::string& name, const std::string& at) {
            GameplayTagError error;
            if (tagSyntax.RegisterTag(name, &error))
                return true;
            result.Error = at + " " + error.Message;
            return false;
        };

        const JsonValue::Array& sections = data.Find("sections")->AsArray();
        if (sections.empty())
            return fail("$.data.sections", "A flow has at least one section.");
        if (sections.size() > kAnimFlowMaxSections)
            return fail("$.data.sections", std::format("A flow has at most {} sections.", kAnimFlowMaxSections));

        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            const JsonValue& entry = sections[i];
            const std::string at = std::format("$.data.sections[{}]", i);
            AnimFlowSectionDecl section;
            section.Tag = Text(&entry, "tag");
            if (!validTag(section.Tag, at + ".tag"))
                return result;
            if (flow->FindSection(section.Tag) >= 0)
                return fail(at + ".tag", std::format("Two sections are tagged '{}'.", section.Tag));
            section.Clip = Text(&entry, "clip");
            section.Slot = Text(&entry, "slot");
            if (section.Clip.empty() == section.Slot.empty())
                return fail(at, "A section plays exactly one of a clip or a slot.");
            if (!section.Clip.empty())
                result.Dependencies.push_back(AssetRef{ AssetType::AnimationClip, section.Clip });
            else if (!validTag(section.Slot, at + ".slot"))
                return result;

            const std::string loop = Text(&entry, "loop");
            section.Loop = loop == "while" ? AnimFlowLoop::While
                : loop == "count"          ? AnimFlowLoop::Count
                                           : AnimFlowLoop::Once;
            if (!ReadAnimPredicate(entry.Find("while"), at + ".while", section.While, result.Error))
                return result;
            if (section.Loop == AnimFlowLoop::While && section.While.Rows.empty())
                return fail(at + ".while", "A while loop needs the condition it repeats on.");
            section.CountIntent = Text(&entry, "count_intent");
            section.CountParam = Text(&entry, "count_param");
            if (section.Loop == AnimFlowLoop::Count && (section.CountIntent.empty() || section.CountParam.empty()))
                return fail(at, "A count loop names the request intent and parameter that give the count.");
            if (const JsonValue* ends = entry.Find("ends"); ends != nullptr && ends->IsBool())
                section.Ends = ends->AsBool();
            section.CancelTiming = Text(&entry, "cancel_timing") == "immediate" ? AnimCancelTiming::Immediate
                                                                                : AnimCancelTiming::AtSectionEnd;
            if (const JsonValue* branches = entry.Find("branches"); branches != nullptr && branches->IsArray())
            {
                for (std::size_t b = 0; b < branches->AsArray().size(); ++b)
                {
                    const JsonValue& branchEntry = branches->AsArray()[b];
                    const std::string branchAt = std::format("{}.branches[{}]", at, b);
                    AnimFlowBranchDecl branch;
                    branch.To = Text(&branchEntry, "to");
                    if (!ReadAnimPredicate(branchEntry.Find("when"), branchAt + ".when", branch.When, result.Error))
                        return result;
                    section.Branches.push_back(std::move(branch));
                }
            }
            flow->Sections.push_back(std::move(section));
        }

        // Only forward: a branch reaches a later section, and nothing earlier.
        for (std::size_t i = 0; i < flow->Sections.size(); ++i)
        {
            for (std::size_t b = 0; b < flow->Sections[i].Branches.size(); ++b)
            {
                const std::string& to = flow->Sections[i].Branches[b].To;
                const int target = flow->FindSection(to);
                const std::string at = std::format("$.data.sections[{}].branches[{}].to", i, b);
                if (target < 0)
                    return fail(at, std::format("No section is tagged '{}'.", to));
                if (target <= static_cast<int>(i))
                    return fail(at, std::format("'{}' is not later in the flow. A flow only goes forward; a "
                                                "sequence that needs to go back is a decision for gameplay, "
                                                "which answers a section's lifecycle event with a new request.",
                                                to));
            }
        }

        flow->Cancel = Text(&data, "cancel");
        if (!flow->Cancel.empty() && flow->FindSection(flow->Cancel) < 0)
            return fail("$.data.cancel", std::format("No section is tagged '{}'.", flow->Cancel));

        for (const auto& [key, out] : { std::pair{ "on_section_entered", &flow->SectionEntered },
                                        std::pair{ "on_section_exited", &flow->SectionExited } })
        {
            const JsonValue* record = data.Find(key);
            if (record == nullptr || !record->IsObject())
                continue;
            AnimLifecycleDecl decl;
            decl.Binding = Text(record, "binding");
            if (decl.Binding.empty())
                return fail(std::format("$.data.{}.binding", key), "A lifecycle event names its binding.");
            decl.Scope = Text(record, "scope") == "gameplay" ? AnimEventScope::Gameplay : AnimEventScope::Cosmetic;
            *out = std::move(decl);
        }

        result.Value = std::move(flow);
        return result;
    }
}

std::string_view AnimFlowLoopName(AnimFlowLoop loop)
{
    switch (loop)
    {
    case AnimFlowLoop::Once: return "once";
    case AnimFlowLoop::While: return "while";
    case AnimFlowLoop::Count: return "count";
    }
    return "?";
}

int AnimFlowData::FindSection(std::string_view tag) const
{
    for (std::size_t i = 0; i < Sections.size(); ++i)
        if (Sections[i].Tag == tag)
            return static_cast<int>(i);
    return -1;
}

void RegisterAnimFlowData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kAnimFlowType), 1, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kAnimFlowType);
}
