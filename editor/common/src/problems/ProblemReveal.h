#pragma once

#include <span>

class IWorkspace;
struct Problem;

bool RevealProblemIn(std::span<IWorkspace* const> workspaces, const Problem& problem);
