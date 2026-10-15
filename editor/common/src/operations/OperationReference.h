#pragma once

#include "EditorOperationCatalog.h"

#include <string>

// Every live operation as compact markdown, by category then name: what it
// does, each parameter's type and meaning, and the keys its answer carries.
// Deterministic, so a checked-in copy can be held to it.
[[nodiscard]] std::string OperationReference(const EditorOperationCatalog& catalog);

// One operation as the reference shows it: what it does, its parameters and
// its answer's keys. A change to it is a change to the operation's contract.
[[nodiscard]] std::string OperationContract(const EditorOperationDefinition& definition);
