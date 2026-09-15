#pragma once

#include <string>
#include <string_view>

//=============================================================================
// The active editor theme as a stylesheet authored surfaces can use.
//
// The editor's chrome theme is presentation policy the host owns. An authored
// document should not carry it, and it has no business travelling through a
// presentation model either -- a model is what a surface presents, and a colour
// is not that.
//
// So the host writes a stylesheet. Documents reference it by the name below and
// style themselves through the classes it defines; the host hands the text to
// UiService::SetHostStyleSheet, and open documents restyle without rebuilding.
// There is no variable system and no styling engine here: the theme already
// holds concrete colours, and this writes concrete rules from them.
//
// **Colours only, never geometry.** An authored document owns its own
// structure, and a theme that moved things would make the same document lay out
// differently depending on which theme happened to be loaded -- which is the
// exact coupling the separation exists to prevent. That also means a process
// which supplies no theme still lays out correctly, just unpainted.
//=============================================================================

// The name packages reference and the host publishes under. One name, because
// documents have to spell it and a second would need a reason.
inline constexpr std::string_view kAuthoredThemeStyleSheetName = "theme.rcss";

[[nodiscard]] std::string BuildAuthoredThemeStyleSheet();
