#include "authoring/AnimationFlowEdits.h"

#include <algorithm>
#include <vector>

namespace
{
    std::string Text(const JsonValue& object, std::string_view key)
    {
        const JsonValue* value = object.Find(key);
        return value != nullptr && value->IsString() ? value->AsString() : std::string();
    }

    void Set(JsonValue& object, std::string_view key, JsonValue value)
    {
        if (JsonValue* existing = object.Find(key))
            *existing = std::move(value);
        else
            object.AsObject().emplace_back(std::string(key), std::move(value));
    }

    void Erase(JsonValue& object, std::string_view key)
    {
        std::erase_if(object.AsObject(), [&](const auto& member) { return member.first == key; });
    }

    JsonValue* Section(JsonValue& root, std::size_t section)
    {
        JsonValue::Array* sections = AnimFlowSections(root);
        return sections != nullptr && section < sections->size() && (*sections)[section].IsObject()
            ? &(*sections)[section]
            : nullptr;
    }

    // Where each section's tag now sits, for checking branches after a move.
    std::ptrdiff_t IndexOf(const JsonValue::Array& sections, std::string_view tag)
    {
        for (std::size_t i = 0; i < sections.size(); ++i)
            if (Text(sections[i], "tag") == tag)
                return static_cast<std::ptrdiff_t>(i);
        return -1;
    }

    bool OnlyForward(const JsonValue::Array& sections)
    {
        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            const JsonValue* branches = sections[i].Find("branches");
            if (branches == nullptr || !branches->IsArray())
                continue;
            for (const JsonValue& branch : branches->AsArray())
                if (IndexOf(sections, Text(branch, "to")) <= static_cast<std::ptrdiff_t>(i))
                    return false;
        }
        return true;
    }
}

JsonValue::Array* AnimFlowSections(JsonValue& root)
{
    JsonValue* data = root.Find("data");
    JsonValue* sections = data != nullptr ? data->Find("sections") : nullptr;
    return sections != nullptr && sections->IsArray() ? &sections->AsArray() : nullptr;
}

void AddAnimFlowSection(JsonValue& root, std::string tag, std::string clip)
{
    if (JsonValue::Array* sections = AnimFlowSections(root))
        sections->emplace_back(JsonValue::Object{ { "tag", JsonValue(std::move(tag)) },
                                                  { "clip", JsonValue(std::move(clip)) } });
}

bool RemoveAnimFlowSection(JsonValue& root, std::size_t section)
{
    JsonValue::Array* sections = AnimFlowSections(root);
    if (sections == nullptr || section >= sections->size())
        return false;
    const std::string tag = Text((*sections)[section], "tag");
    sections->erase(sections->begin() + static_cast<std::ptrdiff_t>(section));
    for (JsonValue& other : *sections)
        if (JsonValue* branches = other.Find("branches"); branches != nullptr && branches->IsArray())
            std::erase_if(branches->AsArray(), [&](const JsonValue& branch) { return Text(branch, "to") == tag; });
    JsonValue* data = root.Find("data");
    if (Text(*data, "cancel") == tag)
        Erase(*data, "cancel");
    return true;
}

bool MoveAnimFlowSection(JsonValue& root, std::size_t from, std::size_t to)
{
    JsonValue::Array* sections = AnimFlowSections(root);
    if (sections == nullptr || from >= sections->size() || to >= sections->size() || from == to)
        return false;
    JsonValue::Array moved = *sections;
    JsonValue section = std::move(moved[from]);
    moved.erase(moved.begin() + static_cast<std::ptrdiff_t>(from));
    moved.insert(moved.begin() + static_cast<std::ptrdiff_t>(to), std::move(section));
    if (!OnlyForward(moved))
        return false;
    *sections = std::move(moved);
    return true;
}

bool SetAnimFlowLoop(JsonValue& root, std::size_t section, std::string_view loop)
{
    JsonValue* entry = Section(root, section);
    if (entry == nullptr || (loop != "once" && loop != "while" && loop != "count"))
        return false;
    if (loop == "once")
        Erase(*entry, "loop");
    else
        Set(*entry, "loop", JsonValue(std::string(loop)));
    if (loop == "while" && entry->Find("while") == nullptr)
        Set(*entry, "while", JsonValue(JsonValue::Array{}));
    if (loop != "while")
        Erase(*entry, "while");
    if (loop != "count")
    {
        Erase(*entry, "count_intent");
        Erase(*entry, "count_param");
    }
    return true;
}

bool SetAnimFlowEnds(JsonValue& root, std::size_t section, bool ends)
{
    JsonValue* entry = Section(root, section);
    if (entry == nullptr)
        return false;
    if (ends)
        Set(*entry, "ends", JsonValue(true));
    else
        Erase(*entry, "ends");
    return true;
}

bool SetAnimFlowCancelImmediately(JsonValue& root, std::size_t section, bool immediately)
{
    JsonValue* entry = Section(root, section);
    if (entry == nullptr)
        return false;
    if (immediately)
        Set(*entry, "cancel_timing", JsonValue(std::string("immediate")));
    else
        Erase(*entry, "cancel_timing");
    return true;
}

bool SetAnimFlowCancel(JsonValue& root, std::string_view tag)
{
    JsonValue::Array* sections = AnimFlowSections(root);
    if (sections == nullptr || (!tag.empty() && IndexOf(*sections, tag) < 0))
        return false;
    JsonValue* data = root.Find("data");
    if (tag.empty())
        Erase(*data, "cancel");
    else
        Set(*data, "cancel", JsonValue(std::string(tag)));
    return true;
}

bool AddAnimFlowBranch(JsonValue& root, std::size_t section, std::size_t to)
{
    JsonValue::Array* sections = AnimFlowSections(root);
    if (sections == nullptr || section >= sections->size() || to >= sections->size() || to <= section)
        return false;
    JsonValue& entry = (*sections)[section];
    JsonValue* branches = entry.Find("branches");
    if (branches == nullptr)
    {
        Set(entry, "branches", JsonValue(JsonValue::Array{}));
        branches = entry.Find("branches");
    }
    branches->AsArray().emplace_back(JsonValue::Object{ { "to", JsonValue(Text((*sections)[to], "tag")) },
                                                        { "when", JsonValue(JsonValue::Array{}) } });
    return true;
}

bool RemoveAnimFlowBranch(JsonValue& root, std::size_t section, std::size_t branch)
{
    JsonValue* entry = Section(root, section);
    JsonValue* branches = entry != nullptr ? entry->Find("branches") : nullptr;
    if (branches == nullptr || !branches->IsArray() || branch >= branches->AsArray().size())
        return false;
    branches->AsArray().erase(branches->AsArray().begin() + static_cast<std::ptrdiff_t>(branch));
    if (branches->AsArray().empty())
        Erase(*entry, "branches");
    return true;
}
