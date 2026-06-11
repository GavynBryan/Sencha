#pragma once

#include <render/TextureData.h>

#include <cstddef>
#include <string_view>
#include <vector>

class LoggingProvider;
class Logger;

//=============================================================================
// TextureSerializer — writes TextureData as a .stex container.
//
// The write half of the .stex round trip (the read half is TextureLoader).
// Rejects structurally invalid TextureData (ValidateTextureData) so a
// malformed cook can never produce a well-formed-looking artifact.
//=============================================================================

// Pure write path (no logging) — usable from importers, which report errors
// instead of logging them. Returns false on invalid texture data.
[[nodiscard]] bool WriteStexToBytes(const TextureData& texture, std::vector<std::byte>& out);

class TextureSerializer
{
public:
    explicit TextureSerializer(LoggingProvider& logging);

    [[nodiscard]] bool WriteToFile(std::string_view path, const TextureData& texture);
    [[nodiscard]] bool WriteToBytes(const TextureData& texture, std::vector<std::byte>& out);

private:
    Logger& Log;
};
