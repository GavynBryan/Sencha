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
#include <optional>
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
                               InputSlotAcceptance acceptance,
                               InputControlCapture& capture,
                               std::string_view documentPath,
                               std::uint64_t documentRevision)
{
    FieldEdit edit;

    // A control pressed on an earlier frame, delivered to the slot that asked
    // for it.
    if (std::optional<std::string> captured =
            capture.TakeCapture(fieldPath, documentPath, documentRevision))
    {
        SetMember(binding, key, JsonValue(*std::move(captured)));
        edit |= FieldEdit::Instant();
    }

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
                // The engine's rule, not a copy of it: a control the binder
                // would reject is never offered, and one it starts accepting
                // appears here without an edit.
                if (!acceptance.Accepts(entry.Control.Source))
                    continue;

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
    const bool listening = capture.IsArmedAt(fieldPath);
    if (ImGui::Button(listening ? "Listening" : "Listen"))
    {
        if (listening)
            capture.Disarm();
        else
            capture.Arm(fieldPath, acceptance, std::string(documentPath), documentRevision);
    }
    ImGui::SetItemTooltip("Press the control you want on this slot. Escape cancels.");

    ImGui::SameLine();
    ImGui::TextUnformatted(label);

    if (listening)
    {
        ImGui::Indent();
        ImGui::TextColored(EditorUi::Accent, "Press a control...");
        ImGui::Unindent();
    }

    // The two mistakes this field invites, said at the field: the bottom
    // validation panel is too far away to connect to what was just typed.
    if (!current.empty())
    {
        const std::optional<InputControl> parsed = ParseInputControl(current);
        ImGui::Indent();
        if (!parsed.has_value())
        {
            ImGui::TextColored(EditorUi::Warning, "'%s' is not a control on this platform.",
                               current.c_str());
        }
        else if (!acceptance.Accepts(parsed->Source))
        {
            ImGui::TextColored(EditorUi::Warning,
                               "'%s' cannot drive this action.", current.c_str());
        }
        ImGui::Unindent();
    }

    ImGui::PopID();
    return edit;
}
