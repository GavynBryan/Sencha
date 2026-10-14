#include <core/assets/AssetIdentity.h>

#include <core/io/FileBytes.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace
{
constexpr std::array<std::string_view, 5> kDocumentsStatingTheirId = { ".smat", ".sscene", ".sdata", ".sworld",
                                                                       ".sanimscenario" };
} // namespace

bool StatesIdInside(std::string_view extension)
{
    return std::ranges::find(kDocumentsStatingTheirId, extension) != kDocumentsStatingTheirId.end();
}

std::filesystem::path SidecarOf(const std::filesystem::path& file)
{
    std::filesystem::path sidecar = file;
    sidecar += std::string(kAssetSidecarSuffix);
    return sidecar;
}

std::filesystem::path IdStatementOf(const std::filesystem::path& file)
{
    return StatesIdInside(file.extension().generic_string()) ? file : SidecarOf(file);
}

std::optional<AssetId> StatedAssetId(const JsonValue& root)
{
    const JsonValue* id = root.IsObject() ? root.Find(kAssetIdKey) : nullptr;
    return id != nullptr && id->IsString() ? AssetIdFromString(id->AsString()) : std::nullopt;
}

void ForgetStatedAssetId(JsonValue& root)
{
    if (root.IsObject())
        std::erase_if(root.AsObject(), [](const auto& member) { return member.first == kAssetIdKey; });
}

std::optional<AssetId> StatedAssetIdIn(std::string_view text)
{
    const std::optional<JsonValue> root = JsonParse(text);
    return root.has_value() ? StatedAssetId(*root) : std::nullopt;
}

std::optional<AssetId> ReadStatedAssetId(const std::filesystem::path& file)
{
    std::vector<std::byte> bytes;
    if (!ReadFileBytes(IdStatementOf(file), bytes))
        return std::nullopt;
    return StatedAssetIdIn(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

std::optional<std::string> StatingAssetId(std::string_view text, AssetId id)
{
    const std::string member = std::string("\"") + std::string(kAssetIdKey) + "\": \"" + AssetIdToString(id) + "\"";
    if (text.find_first_not_of(" \t\r\n") == std::string_view::npos)
        return "{\n  " + member + "\n}\n";
    const std::size_t open = text.find_first_not_of(" \t\r\n");
    if (text[open] != '{')
        return std::nullopt;
    const std::size_t next = text.find_first_not_of(" \t\r\n", open + 1);
    if (next == std::string_view::npos)
        return std::nullopt;
    std::string stated(text.substr(0, open + 1));
    if (text[next] == '}')
        stated += " " + member + " ";
    else
    {
        // The next member's line, so the id sits indented as its siblings are.
        const std::size_t lineStart = text.rfind('\n', next);
        const bool ownLine = lineStart != std::string_view::npos && lineStart > open;
        const std::string_view indent = ownLine ? text.substr(lineStart + 1, next - lineStart - 1) : std::string_view(" ");
        stated += (ownLine ? "\n" : "") + std::string(indent) + member + ",";
    }
    stated += text.substr(open + 1);
    return stated;
}
