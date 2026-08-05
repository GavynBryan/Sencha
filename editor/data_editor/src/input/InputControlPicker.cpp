#include "input/InputControlPicker.h"

#include "JsonObjectEdit.h"
#include "input/InputBindingSummary.h"

#include "ui/EditorUiStyle.h"

#include <input/InputControl.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string>

namespace
{
// Filter state lives per popup rather than per slot: only one popup is open at
// a time, and carrying a stale filter into the next one would hide everything.
std::array<char, 64> FilterText{};

bool MatchesFilter(std::string_view friendly, std::string_view authored, std::string_view filter)
{
    if (filter.empty())
        return true;

    // Both spellings match, because an author who knows "key.w" should not have
    // to remember it is labelled "W".
    const auto contains = [](std::string_view haystack, std::string_view needle)
    {
        const auto it = std::search(haystack.begin(), haystack.end(),
                                    needle.begin(), needle.end(),
                                    [](char a, char b)
                                    {
                                        return std::tolower(static_cast<unsigned char>(a))
                                            == std::tolower(static_cast<unsigned char>(b));
                                    });
        return it != haystack.end();
    };
    return contains(friendly, filter) || contains(authored, filter);
}

const char* DeviceHeading(InputControlSource source)
{
    switch (source)
    {
    case InputControlSource::Key:
        return "Keyboard";
    case InputControlSource::MouseButton:
    case InputControlSource::MouseMotion:
    case InputControlSource::MouseWheel:
        return "Mouse";
    default:
        return "Gamepad";
    }
}
}

FieldEdit DrawInputControlSlot(JsonValue& binding,
                               std::string_view key,
                               const char* label,
                               const std::string& fieldPath,
                               InputControlSlotFilter filter)
{
    FieldEdit edit;
    const std::string current = ReadMemberString(binding, key);

    ImGui::PushID(fieldPath.c_str());

    std::array<char, 256> buffer{};
    const std::size_t count = std::min(current.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), current.c_str(), count);

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.34f);
    const bool edited = ImGui::InputText("##control", buffer.data(), buffer.size());
    if (edited)
        SetMember(binding, key, JsonValue(std::string(buffer.data())));
    edit |= FieldEdit{ edited, ImGui::IsItemDeactivatedAfterEdit() };

    ImGui::SameLine();
    if (ImGui::Button("Pick"))
    {
        FilterText.fill('\0');
        ImGui::OpenPopup("pickcontrol");
    }

    if (ImGui::BeginPopup("pickcontrol"))
    {
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##filter", "Filter", FilterText.data(), FilterText.size());
        ImGui::Separator();

        const std::string_view needle{ FilterText.data() };
        if (ImGui::BeginChild("controls", ImVec2(280.0f, 320.0f)))
        {
            const char* heading = nullptr;
            for (const NamedInputControl& entry : EnumerateInputControls())
            {
                if (filter == InputControlSlotFilter::ButtonsOnly
                    && ValueKindOf(entry.Control.Source) != InputControlValueKind::Button)
                {
                    continue;
                }

                const std::string friendly = DescribeInputControlName(entry.Name);
                if (!MatchesFilter(friendly, entry.Name, needle))
                    continue;

                if (const char* device = DeviceHeading(entry.Control.Source); device != heading)
                {
                    heading = device;
                    ImGui::SeparatorText(device);
                }

                if (ImGui::Selectable(friendly.c_str(), entry.Name == current))
                {
                    SetMember(binding, key, JsonValue(entry.Name));
                    edit |= FieldEdit::Instant();
                    ImGui::CloseCurrentPopup();
                }
                // The authored spelling beside the label, so picking one teaches
                // the name to type next time.
                ImGui::SameLine();
                ImGui::TextDisabled("%s", entry.Name.c_str());
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(label);

    // A name nothing recognizes is the author's most common mistake here, and
    // the bottom validation panel is too far from the field to connect them.
    if (!current.empty() && !ParseInputControl(current).has_value())
    {
        ImGui::Indent();
        ImGui::TextColored(EditorUi::Warning, "'%s' is not a control on this platform.",
                           current.c_str());
        ImGui::Unindent();
    }

    ImGui::PopID();
    return edit;
}
