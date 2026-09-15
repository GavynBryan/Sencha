#pragma once

#include <assets/ui/UiPackage.h>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

//=============================================================================
// .sui round trip.
//
// Both halves are pure functions -- no logging, no engine state. The write half
// runs inside the importer, which reports errors rather than logging them; the
// read half runs in the package stage half, which may be on a task thread.
//=============================================================================

// Writes `package` as a .sui container. Returns false on a package that could
// not be read back as itself: no root name, no blobs, or a root name naming no
// blob.
[[nodiscard]] bool WriteSuiToBytes(const UiPackage& package, std::vector<std::byte>& out);

// Parses a .sui container. Rejects malformed input -- bad magic, unknown
// version, a record claiming more bytes than remain -- rather than patching
// around it, because a half-read document is worse than a missing one. Errors
// travel in `error`.
[[nodiscard]] bool LoadSuiFromBytes(std::span<const std::byte> bytes,
                                    UiPackage& out,
                                    std::string* error = nullptr);
