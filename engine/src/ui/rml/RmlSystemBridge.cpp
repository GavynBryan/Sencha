#include "RmlSystemBridge.h"

#include "../UiDiagnosticLog.h"

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
