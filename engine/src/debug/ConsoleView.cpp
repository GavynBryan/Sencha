#include <debug/ConsoleView.h>

#include <core/console/ConsoleCompletion.h>
#include <core/console/ConsoleService.h>
#include <debug/DebugLogEntry.h>
#include <debug/DebugLogSink.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace
{
    struct InputCallbackData
    {
        ConsoleViewState* State;
        const ConsoleRegistry* Registry;
    };

    // Start index of the token under the cursor (the run after the last space).
    std::size_t LastTokenStart(std::string_view text)
    {
        const std::size_t ws = text.find_last_of(" \t");
        return ws == std::string_view::npos ? 0 : ws + 1;
    }

    void RecomputeSuggestions(ConsoleViewState& state,
                              const ConsoleRegistry& registry,
                              std::string_view line)
    {
        // No popup on a blank line: there, Up/Down recall history instead.
        state.Suggestions = line.empty() ? std::vector<std::string>{}
                                         : SuggestConsoleCompletions(registry, line);
        state.SelectedSuggestion = -1;
    }

    // Buffer-side accept (click path): replace the token under the cursor with the
    // chosen suggestion plus a trailing space, then recompute the next level. The
    // Tab path edits the active widget instead (see AcceptInCallback).
    void AcceptSuggestion(ConsoleViewState& state, const ConsoleRegistry& registry, int index)
    {
        if (index < 0 || index >= static_cast<int>(state.Suggestions.size()))
            return;
        std::string text(state.Input);
        const std::size_t start = LastTokenStart(text);
        text.replace(start, std::string::npos, state.Suggestions[static_cast<std::size_t>(index)]);
        text.push_back(' ');
        std::snprintf(state.Input, sizeof(state.Input), "%s", text.c_str());
        RecomputeSuggestions(state, registry, state.Input);
    }

    // Tab acceptance: replace the current token with the chosen suggestion, in
    // place, so the cursor and focus stay put.
    void AcceptInCallback(ImGuiInputTextCallbackData* data,
                          ConsoleViewState& state,
                          const ConsoleRegistry& registry)
    {
        if (state.Suggestions.empty())
            return;
        const int index = state.SelectedSuggestion >= 0 ? state.SelectedSuggestion : 0;
        if (index >= static_cast<int>(state.Suggestions.size()))
            return;
        const std::string& pick = state.Suggestions[static_cast<std::size_t>(index)];

        const std::string_view current(data->Buf, static_cast<std::size_t>(data->BufTextLen));
        const int start = static_cast<int>(LastTokenStart(current));
        data->DeleteChars(start, data->BufTextLen - start);
        data->InsertChars(start, pick.c_str());
        data->InsertChars(data->CursorPos, " ");

        RecomputeSuggestions(state, registry,
                             std::string_view(data->Buf, static_cast<std::size_t>(data->BufTextLen)));
    }

    int InputTextCallback(ImGuiInputTextCallbackData* data)
    {
        auto* cb = static_cast<InputCallbackData*>(data->UserData);
        ConsoleViewState& state = *cb->State;
        const ConsoleRegistry& registry = *cb->Registry;

        switch (data->EventFlag)
        {
        case ImGuiInputTextFlags_CallbackEdit:
            RecomputeSuggestions(state, registry,
                                 std::string_view(data->Buf, static_cast<std::size_t>(data->BufTextLen)));
            state.History.ResetCursor();
            break;

        case ImGuiInputTextFlags_CallbackCompletion:
            AcceptInCallback(data, state, registry);
            break;

        case ImGuiInputTextFlags_CallbackHistory:
            if (!state.Suggestions.empty())
            {
                // Popup open: arrows move the highlight, wrapping around.
                const int count = static_cast<int>(state.Suggestions.size());
                if (data->EventKey == ImGuiKey_UpArrow)
                    state.SelectedSuggestion =
                        state.SelectedSuggestion <= 0 ? count - 1 : state.SelectedSuggestion - 1;
                else if (data->EventKey == ImGuiKey_DownArrow)
                    state.SelectedSuggestion = (state.SelectedSuggestion + 1) % count;
            }
            else
            {
                // No popup: arrows recall command history into the buffer.
                std::optional<std::string_view> entry;
                if (data->EventKey == ImGuiKey_UpArrow)
                    entry = state.History.Prev();
                else if (data->EventKey == ImGuiKey_DownArrow)
                    entry = state.History.Next();
                data->DeleteChars(0, data->BufTextLen);
                if (entry)
                {
                    const std::string text(*entry);
                    data->InsertChars(0, text.c_str());
                }
            }
            break;

        default:
            break;
        }
        return 0;
    }
}

void DrawConsoleView(DebugLogSink& sink,
                     ConsoleService& console,
                     ConsoleViewState& state,
                     const ConsoleViewStyle& style)
{
    ConsoleRegistry& registry = console.Registry();

    if (!state.Initialized)
    {
        if (const CVarMetadata* cvar = registry.FindCVar("console.history_capacity");
            cvar != nullptr && std::holds_alternative<std::int64_t>(cvar->CurrentValue))
        {
            const std::int64_t capacity = std::get<std::int64_t>(cvar->CurrentValue);
            if (capacity > 0)
                state.History = ConsoleHistory(static_cast<std::size_t>(capacity));
        }
        state.Initialized = true;
    }

    // -- Toolbar --------------------------------------------------------------
    if (ImGui::Button("Clear"))
    {
        sink.Clear();
        state.Transcript.clear();
    }
    bool copyRequested = false;
    if (style.ShowCopy)
    {
        ImGui::SameLine();
        // ImGui text isn't selectable; copy the visible (filtered) transcript via
        // LogToClipboard, which captures whatever is rendered until LogFinish.
        copyRequested = ImGui::Button("Copy");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copy the visible log to the clipboard");
    }
    static constexpr const char* LevelNames[] = { "Debug", "Info", "Warn", "Error", "Crit" };
    for (int i = 0; i < 5; ++i)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, style.LevelColors[static_cast<std::size_t>(i)]);
        ImGui::Checkbox(LevelNames[i], &state.LevelFilter[static_cast<std::size_t>(i)]);
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputText("Category", state.CategoryFilter, sizeof(state.CategoryFilter));

    ImGui::Separator();

    // The output fills everything above the input row. The autosuggest list is a
    // floating overlay (drawn last, positioned above the input) so showing or
    // hiding it never reflows the input or the log.
    const float footer = ImGui::GetFrameHeightWithSpacing();

    // -- Output (engine log feed, then the command transcript) ----------------
    ImGui::BeginChild("##console_output", ImVec2(0.0f, -footer), false,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs);

    const bool stick = state.ScrollToBottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
    state.ScrollToBottom = false;

    if (style.Mono != nullptr)
        ImGui::PushFont(style.Mono);
    if (copyRequested)
        ImGui::LogToClipboard();

    const std::string_view categoryFilter(state.CategoryFilter);
    for (std::size_t i = 0; i < sink.Count(); ++i)
    {
        const DebugLogEntry& entry = sink.GetEntry(i);
        const int levelIdx = static_cast<int>(entry.Level);
        if (levelIdx < 0 || levelIdx >= 5)
            continue;
        if (!state.LevelFilter[static_cast<std::size_t>(levelIdx)])
            continue;
        if (!categoryFilter.empty() && entry.Category.find(categoryFilter) == std::string::npos)
            continue;
        ImGui::PushStyleColor(ImGuiCol_Text, style.LevelColors[static_cast<std::size_t>(levelIdx)]);
        ImGui::Text("[%s] %s", entry.Category.c_str(), entry.Message.c_str());
        ImGui::PopStyleColor();
    }
    for (const ConsoleOutputEntry& entry : state.Transcript)
    {
        ImVec4 color = style.TextPrimary;
        if (entry.Severity == ConsoleOutputSeverity::Warning)
            color = style.Warning;
        else if (entry.Severity == ConsoleOutputSeverity::Error)
            color = style.Error;
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(entry.Text.c_str());
        ImGui::PopStyleColor();
    }

    if (copyRequested)
        ImGui::LogFinish();
    if (style.Mono != nullptr)
        ImGui::PopFont();
    if (stick)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    // -- Input box ------------------------------------------------------------
    if (state.RefocusInput)
    {
        ImGui::SetKeyboardFocusHere();
        state.RefocusInput = false;
    }
    ImGui::SetNextItemWidth(-1.0f);
    InputCallbackData cb{ &state, &registry };
    const ImGuiInputTextFlags inputFlags =
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory
        | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackEdit;
    if (style.Mono != nullptr)
        ImGui::PushFont(style.Mono);
    const bool submitted = ImGui::InputText("##console_input", state.Input, sizeof(state.Input),
                                            inputFlags, &InputTextCallback, &cb);
    if (style.Mono != nullptr)
        ImGui::PopFont();
    const ImVec2 inputMin = ImGui::GetItemRectMin();
    const ImVec2 inputSize = ImGui::GetItemRectSize();

    if (submitted && state.Input[0] != '\0')
    {
        state.Transcript.push_back({
            .Severity = ConsoleOutputSeverity::Info,
            .Channel = "input",
            .Text = std::string("> ") + state.Input,
        });
        const ConsoleResult result =
            console.ExecuteLine(state.Input, { .Description = "console ui" }, false);
        state.Transcript.insert(state.Transcript.end(), result.Output.begin(), result.Output.end());
        state.History.Push(state.Input);
        state.Input[0] = '\0';
        state.Suggestions.clear();
        state.SelectedSuggestion = -1;
        state.ScrollToBottom = true;
        state.RefocusInput = true;
    }

    // -- Autosuggest overlay --------------------------------------------------
    // Floats just above the input, drawn last (as a sibling child submitted after
    // the output, so it paints over the log) and positioned absolutely, so it
    // never takes layout space. Focus stays in the input; arrows/Tab drive it from
    // the input callback, and a click accepts a row.
    const int shownSuggestions = std::min<int>(static_cast<int>(state.Suggestions.size()), 8);
    if (shownSuggestions > 0)
    {
        const ImGuiStyle& imStyle = ImGui::GetStyle();
        const float popupHeight =
            shownSuggestions * ImGui::GetTextLineHeightWithSpacing() + imStyle.WindowPadding.y * 2.0f;
        ImGui::SetCursorScreenPos(
            ImVec2(inputMin.x, inputMin.y - popupHeight - imStyle.ItemSpacing.y));

        ImVec4 popupBg = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
        popupBg.w = 1.0f; // opaque so the log behind it does not bleed through

        ImGui::PushStyleColor(ImGuiCol_ChildBg, popupBg);
        ImGui::BeginChild("##console_suggest", ImVec2(inputSize.x, popupHeight), true,
                          ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoScrollbar
                              | ImGuiWindowFlags_NoSavedSettings);
        if (style.Mono != nullptr)
            ImGui::PushFont(style.Mono);
        for (int i = 0; i < static_cast<int>(state.Suggestions.size()); ++i)
        {
            const bool selected = (i == state.SelectedSuggestion);
            if (ImGui::Selectable(state.Suggestions[static_cast<std::size_t>(i)].c_str(), selected))
            {
                // Click accepts the row (Tab does the same via the input callback).
                // No refocus: SetKeyboardFocusHere selects-all and would wipe it.
                AcceptSuggestion(state, registry, i);
                break; // Suggestions was just rebuilt; stop iterating the old list.
            }
            if (selected)
                ImGui::SetScrollHereY(0.5f);
        }
        if (style.Mono != nullptr)
            ImGui::PopFont();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
}
