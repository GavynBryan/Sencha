#include "RmlSystemBridge.h"

#include "../UiDiagnosticLog.h"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>

#include <optional>
#include <string>
#include <string_view>

namespace
{
    // The text between the first pair of single quotes after `marker`.
    std::optional<std::string> Quoted(std::string_view message, std::string_view marker)
    {
        const std::size_t at = message.find(marker);
        if (at == std::string_view::npos)
            return std::nullopt;
        // The marker ends in the opening quote; the value runs to the next one.
        const std::size_t open = message.find('\'', at);
        if (open == std::string_view::npos)
            return std::nullopt;
        const std::size_t close = message.find('\'', open + 1);
        if (close == std::string_view::npos)
            return std::nullopt;
        return std::string(message.substr(open + 1, close - open - 1));
    }

    // What the document engine's message means, when it is one this layer
    // recognises. The wordings are the vendored engine's (DataModel.cpp,
    // DataVariable.cpp, DataExpression.cpp); anything else is passed through
    // as itself.
    UiDiagnosticKind Classify(std::string_view message, std::optional<std::string>& variable)
    {
        if (auto v = Quoted(message, "Could not find variable name '"))
        {
            variable = std::move(v);
            return UiDiagnosticKind::BindingMissing;
        }
        if (auto v = Quoted(message, "Could not get value from data variable '"))
        {
            variable = std::move(v);
            return UiDiagnosticKind::BindingMissing;
        }
        if (auto v = Quoted(message, "Could not find data variable with name '"))
        {
            variable = std::move(v);
            return UiDiagnosticKind::BindingMissing;
        }
        if (auto v = Quoted(message, "Could not find data event callback '"))
        {
            variable = std::move(v);
            return UiDiagnosticKind::EventCallbackMissing;
        }
        constexpr std::string_view kMember = "Member ";
        constexpr std::string_view kNotFound = " not found in data struct";
        if (message.rfind(kMember, 0) == 0)
        {
            const std::size_t end = message.find(kNotFound);
            if (end != std::string_view::npos && end > kMember.size())
            {
                variable = std::string(message.substr(kMember.size(), end - kMember.size()));
                return UiDiagnosticKind::MemberMissing;
            }
        }
        return UiDiagnosticKind::DocumentEngineOther;
    }
}

RmlSystemBridge::RmlSystemBridge(UiDiagnosticLog& diagnostics)
    : Diagnostics(diagnostics)
    , Start(std::chrono::steady_clock::now())
{
}

double RmlSystemBridge::GetElapsedTime()
{
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - Start;
    return elapsed.count();
}

bool RmlSystemBridge::LogMessage(Rml::Log::Type type, const Rml::String& message)
{
    UiDiagnostic entry;
    entry.Source = UiDiagnosticSource::DocumentEngine;
    switch (type)
    {
    case Rml::Log::LT_ALWAYS:
    case Rml::Log::LT_ERROR:
    case Rml::Log::LT_ASSERT:
        entry.Severity = UiDiagnosticSeverity::Error;
        break;
    case Rml::Log::LT_WARNING:
        entry.Severity = UiDiagnosticSeverity::Warning;
        break;
    case Rml::Log::LT_INFO:
    case Rml::Log::LT_DEBUG:
    case Rml::Log::LT_MAX:
    default:
        entry.Severity = UiDiagnosticSeverity::Info;
        break;
    }
    entry.Kind = Classify(message, entry.Variable);
    entry.Message = "ui: " + message;
    Diagnostics.Push(std::move(entry));

    // False would abort the process on an assert-level message. An authoring
    // mistake in a document is a diagnostic, never a reason to take the game
    // down with it.
    return true;
}

RmlSystemBridge::~RmlSystemBridge()
{
    for (SDL_Cursor* cursor : Cursors)
    {
        if (cursor != nullptr)
            SDL_DestroyCursor(cursor);
    }
}

void RmlSystemBridge::SetClipboardText(const Rml::String& text)
{
    if (SDL_WasInit(SDL_INIT_VIDEO) != 0)
        (void)SDL_SetClipboardText(text.c_str());
}

void RmlSystemBridge::GetClipboardText(Rml::String& text)
{
    text.clear();
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
        return;
    if (char* clipboard = SDL_GetClipboardText(); clipboard != nullptr)
    {
        text = clipboard;
        SDL_free(clipboard);
    }
}

namespace
{
    // The CSS cursor names a document writes, as the shapes SDL offers.
    SDL_SystemCursor SystemCursorFor(std::string_view name)
    {
        if (name == "pointer" || name == "hand")
            return SDL_SYSTEM_CURSOR_POINTER;
        if (name == "text")
            return SDL_SYSTEM_CURSOR_TEXT;
        if (name == "move" || name == "grab" || name == "grabbing")
            return SDL_SYSTEM_CURSOR_MOVE;
        if (name == "crosshair")
            return SDL_SYSTEM_CURSOR_CROSSHAIR;
        if (name == "ew-resize" || name == "col-resize")
            return SDL_SYSTEM_CURSOR_EW_RESIZE;
        if (name == "ns-resize" || name == "row-resize")
            return SDL_SYSTEM_CURSOR_NS_RESIZE;
        if (name == "nwse-resize")
            return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
        if (name == "nesw-resize")
            return SDL_SYSTEM_CURSOR_NESW_RESIZE;
        if (name == "not-allowed")
            return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
        if (name == "wait")
            return SDL_SYSTEM_CURSOR_WAIT;
        if (name == "progress")
            return SDL_SYSTEM_CURSOR_PROGRESS;
        return SDL_SYSTEM_CURSOR_DEFAULT;
    }
}

void RmlSystemBridge::SetMouseCursor(const Rml::String& cursorName)
{
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
        return;
    const SDL_SystemCursor shape = SystemCursorFor(cursorName);
    SDL_Cursor*& cursor = Cursors[static_cast<std::size_t>(shape)];
    if (cursor == nullptr)
        cursor = SDL_CreateSystemCursor(shape);
    if (cursor != nullptr)
        (void)SDL_SetCursor(cursor);
}
