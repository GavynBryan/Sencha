#include "ui/AnimationDocumentWidgets.h"

#include "authoring/AnimationPredicateEdits.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace
{
constexpr std::array<const char*, 6> kCompares{ "lt", "le", "gt", "ge", "eq", "ne" };
constexpr std::array<const char*, 6> kCompareSymbols{ "<", "<=", ">", ">=", "==", "!=" };
constexpr std::array<const char*, 3> kMatches{ "all", "any", "none" };
constexpr std::array<const char*, 4> kRequestTests{ "active", "age", "param", "cancelled" };
constexpr std::array<const char*, 4> kReasons{ "released", "interrupted", "failed", "superseded" };
}

namespace AnimationWidgets
{
std::string Text(const JsonValue& object, std::string_view key)
{
    const JsonValue* value = object.Find(key);
    return value != nullptr && value->IsString() ? value->AsString() : std::string();
}

double Number(const JsonValue& object, std::string_view key, double fallback)
{
    const JsonValue* value = object.Find(key);
    return value != nullptr && value->IsNumber() ? value->AsNumber() : fallback;
}

void SetMember(JsonValue& object, std::string_view key, JsonValue value)
{
    if (JsonValue* existing = object.Find(key))
        *existing = std::move(value);
    else
        object.AsObject().emplace_back(std::string(key), std::move(value));
}

void EraseMember(JsonValue& object, std::string_view key)
{
    std::erase_if(object.AsObject(), [&](const auto& member) { return member.first == key; });
}

void TextMember(const char* label, JsonValue& object, std::string_view key, Edit& edit, float width)
{
    std::array<char, 256> buffer{};
    const std::string current = Text(object, key);
    std::memcpy(buffer.data(), current.data(), std::min(current.size(), buffer.size() - 1));
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputText(label, buffer.data(), buffer.size()))
    {
        SetMember(object, key, JsonValue(std::string(buffer.data())));
        edit.Changed = true;
    }
    edit.Commit |= ImGui::IsItemDeactivatedAfterEdit();
}

void NumberMember(const char* label, JsonValue& object, std::string_view key, Edit& edit, float speed, float width)
{
    float value = static_cast<float>(Number(object, key));
    ImGui::SetNextItemWidth(width);
    if (ImGui::DragFloat(label, &value, speed))
    {
        SetMember(object, key, JsonValue(static_cast<double>(value)));
        edit.Changed = true;
    }
    edit.Commit |= ImGui::IsItemDeactivatedAfterEdit();
}

void NameMember(const char* label, JsonValue& object, std::string_view key, const std::vector<std::string>& names,
                Edit& edit)
{
    if (names.empty())
    {
        TextMember(label, object, key, edit);
        return;
    }
    const std::string current = Text(object, key);
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo(label, current.empty() ? "(choose)" : current.c_str()))
    {
        for (const std::string& name : names)
            if (ImGui::Selectable(name.c_str(), name == current))
            {
                SetMember(object, key, JsonValue(name));
                edit.Instant();
            }
        ImGui::EndCombo();
    }
}

void CompareAndValue(JsonValue& test, bool tagValued, Edit& edit)
{
    ImGui::SameLine();
    ChoiceMember("##compare", test, "compare", kCompares, kCompareSymbols, edit, 50.0f);
    ImGui::SameLine();
    if (tagValued)
        TextMember("##tag", test, "tag", edit, 150.0f);
    else
        NumberMember("##value", test, "value", edit);
}

// One test, with controls for what it reads.
void DrawTest(JsonValue& test, const PredicateVocabulary& vocabulary, Edit& edit)
{
    if (!test.IsObject())
    {
        ImGui::TextDisabled("(not a test)");
        return;
    }
    bool negate = test.Find("not") != nullptr && test.Find("not")->IsBool() && test.Find("not")->AsBool();
    if (ImGui::Checkbox("not", &negate))
    {
        if (negate)
            SetMember(test, "not", JsonValue(true));
        else
            EraseMember(test, "not");
        edit.Instant();
    }
    ImGui::SameLine();
    if (test.Find("request") != nullptr)
    {
        const std::string intent = Text(test, "request");
        NameMember("##intent", test, "request", vocabulary.Intents(), edit);
        ImGui::SameLine();
        const std::string kind = Text(test, "test").empty() ? "active" : Text(test, "test");
        ChoiceMember("##test", test, "test", kRequestTests, kRequestTests, edit, 90.0f);
        if (kind == "age")
            CompareAndValue(test, false, edit);
        else if (kind == "param")
        {
            ImGui::SameLine();
            NameMember("##param", test, "param", vocabulary.Params(intent), edit);
            CompareAndValue(test, !Text(test, "tag").empty(), edit);
        }
        else if (kind == "cancelled")
        {
            ImGui::SameLine();
            ChoiceMember("##reason", test, "reason", kReasons, kReasons, edit, 100.0f);
        }
        return;
    }
    if (test.Find("elapsed") != nullptr)
    {
        ImGui::TextUnformatted("time in behavior");
        CompareAndValue(test, false, edit);
        return;
    }
    const std::string fact = Text(test, "fact");
    NameMember("##fact", test, "fact", vocabulary.Facts(), edit);
    const AnimFactKind kind = vocabulary.KindOf(fact);
    if (test.Find("has") != nullptr || kind == AnimFactKind::TagSet)
    {
        ImGui::SameLine();
        ChoiceMember("##has", test, "has", kMatches, kMatches, edit, 60.0f);
        // The query as comma-separated tags.
        std::string joined;
        if (const JsonValue* query = test.Find("query"); query != nullptr && query->IsArray())
            for (const JsonValue& tag : query->AsArray())
                if (tag.IsString())
                    joined += (joined.empty() ? "" : ", ") + tag.AsString();
        std::array<char, 256> buffer{};
        std::memcpy(buffer.data(), joined.data(), std::min(joined.size(), buffer.size() - 1));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::InputText("##query", buffer.data(), buffer.size()))
        {
            JsonValue::Array tags;
            std::string text(buffer.data());
            std::size_t start = 0;
            while (start <= text.size())
            {
                const std::size_t comma = std::min(text.find(',', start), text.size());
                std::string tag = text.substr(start, comma - start);
                tag.erase(0, tag.find_first_not_of(' '));
                tag.erase(tag.find_last_not_of(' ') + 1);
                if (!tag.empty())
                    tags.emplace_back(tag);
                start = comma + 1;
            }
            SetMember(test, "query", JsonValue(std::move(tags)));
            edit.Changed = true;
        }
        edit.Commit |= ImGui::IsItemDeactivatedAfterEdit();
        return;
    }
    if (kind == AnimFactKind::Bool && test.Find("compare") == nullptr)
        return;
    CompareAndValue(test, kind == AnimFactKind::Tag, edit);
}

void DrawPredicate(JsonValue::Array& rows, const PredicateVocabulary& vocabulary, Edit& edit)
{
    const std::size_t count = rows.size();
    if (count == 0)
        ImGui::TextDisabled("always");
    for (std::size_t r = 0; r < count; ++r)
    {
        ImGui::PushID(static_cast<int>(r));
        JsonValue& row = rows[r];
        if (r > 0)
            ImGui::TextDisabled("and");
        if (JsonValue* any = row.Find("any"); any != nullptr && any->IsArray())
        {
            for (std::size_t a = 0; a < any->AsArray().size(); ++a)
            {
                ImGui::PushID(static_cast<int>(a));
                if (a > 0)
                {
                    ImGui::TextDisabled("  or");
                }
                DrawTest(any->AsArray()[a], vocabulary, edit);
                ImGui::SameLine();
                if (ImGui::SmallButton("x"))
                {
                    (void)RemoveAnimPredicateAlternative(rows, r, a);
                    edit.Instant();
                    ImGui::PopID();
                    ImGui::PopID();
                    return;
                }
                ImGui::PopID();
            }
        }
        else
        {
            DrawTest(row, vocabulary, edit);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+ or"))
        {
            // The new alternative starts as a copy of the row's first test.
            JsonValue first = row.Find("any") != nullptr ? row.Find("any")->AsArray().front() : row;
            (void)AddAnimPredicateAlternative(rows, r, std::move(first));
            edit.Instant();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("remove"))
        {
            (void)RemoveAnimPredicateRow(rows, r);
            edit.Instant();
            ImGui::PopID();
            return;
        }
        ImGui::PopID();
    }

    if (ImGui::SmallButton("+ condition"))
        ImGui::OpenPopup("add");
    if (ImGui::BeginPopup("add"))
    {
        if (vocabulary.Rig != nullptr && ImGui::BeginMenu("Fact"))
        {
            for (const AnimBoundFactSlot& slot : vocabulary.Rig->Slots)
                if (ImGui::MenuItem(slot.Name.c_str()))
                {
                    AddAnimPredicateRow(rows, MakeAnimFactTest(slot.Name, slot.Kind));
                    edit.Instant();
                }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Request"))
        {
            for (const std::string& intent : vocabulary.Intents())
                if (ImGui::MenuItem(intent.c_str()))
                {
                    AddAnimPredicateRow(rows, MakeAnimRequestTest(intent));
                    edit.Instant();
                }
            if (vocabulary.Intents().empty() && ImGui::MenuItem("(type an intent)"))
            {
                AddAnimPredicateRow(rows, MakeAnimRequestTest(""));
                edit.Instant();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Time in behavior"))
        {
            AddAnimPredicateRow(rows, MakeAnimElapsedTest(1.0));
            edit.Instant();
        }
        if (vocabulary.Rig == nullptr && ImGui::MenuItem("Fact (type a name)"))
        {
            AddAnimPredicateRow(rows, MakeAnimFactTest("", AnimFactKind::Bool));
            edit.Instant();
        }
        ImGui::EndPopup();
    }
}
}
