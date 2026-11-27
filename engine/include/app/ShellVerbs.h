#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/VerbInvocation.h>

#include <string_view>

class Engine;
class PauseMenu;

//=============================================================================
// The host's implementations of the engine's own verbs
//
// Two small concrete objects rather than one "shell operations" class: they
// share a file because they are the stock host's answer to the stock
// vocabulary and always change together, and nothing else about them is
// common. Each holds exactly the one owner it needs.
//
// Both are requests. Neither does the thing inside dispatch: resuming would
// destroy the page the request arrived through, and exiting from inside a
// document's event handler would tear down the engine underneath it. The
// deferral is not a scheduling policy a caller chose -- it is what these
// operations mean.
//=============================================================================

// The engine's own binding asset, and the keys the stock entries address in it.
//
// The association between a menu entry and an operation is content: this names
// which record, and the record names which verb. Nothing here relates a command
// id to a verb, which is what keeps adding an authored entry from being an
// engine edit.
inline constexpr std::string_view kShellBindingsAsset = "asset://data/shell.bindings.sdata";
inline constexpr std::string_view kShellResumeBinding = "shell.resume";
inline constexpr std::string_view kShellQuitBinding = "shell.quit";

// Asks the shell to leave its pages at the end of the frame's update, which is
// the existing deferred resume PauseMenu already owns. Page closure and the
// pause transition happen after the drain, exactly as they do for a native
// handler, so a zero-tick frame resumes in the same frame it was asked.
class RuntimeResumeOperation
{
public:
    explicit RuntimeResumeOperation(PauseMenu& menu) : Menu(&menu) {}

    // What resuming does to the world is PauseState's answer, not a caller's choice.
    SENCHA_VERB("runtime.resume")
    SENCHA_LABEL("Resume")
    SENCHA_CATEGORY("Runtime")
    SENCHA_DESCRIPTION("Closes the application shell and resumes local play.")
    VerbAdmission Resume();

private:
    PauseMenu* Menu;
};

// Asks the host to exit, attributed to the menu, so a game's exit handler can
// answer a shell request differently from a console command.
class ApplicationQuitOperation
{
public:
    explicit ApplicationQuitOperation(Engine& engine) : Host(&engine) {}

    SENCHA_VERB("application.quit")
    SENCHA_LABEL("Quit")
    SENCHA_CATEGORY("Application")
    SENCHA_DESCRIPTION("Asks the host to exit. The game's exit handler still sees the request.")
    VerbAdmission Quit();

private:
    Engine* Host;
};

#if !defined(SENCHA_CODEGEN)
#  include <app/ShellVerbs.sencha.h>
#endif
