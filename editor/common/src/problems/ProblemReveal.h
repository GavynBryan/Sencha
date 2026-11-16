#pragma once

#include <span>

class IWorkspace;
struct Problem;

// Asks `workspaces`, in the order given, to show where `problem` is: those that
// own its document first, then any that can open it. True when one did.
bool RevealProblemIn(std::span<IWorkspace* const> workspaces, const Problem& problem);
