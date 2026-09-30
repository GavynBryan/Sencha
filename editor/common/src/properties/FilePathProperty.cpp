#include "properties/FilePathProperty.h"

#include "properties/SettingProperties.h"

namespace
{
constexpr std::string_view kScheme = "://";

std::string RootRelative(const std::string& path)
{
    const std::size_t scheme = path.find(kScheme);
    return scheme == std::string::npos ? path : path.substr(scheme + kScheme.size());
}
} // namespace

std::vector<PropertyNode> FilePathProperty::Nodes() const
{
    const std::string path = Path ? Path() : std::string{};
    if (path.empty())
        return {};
    return { TextProperty("path", "File", RootRelative(path),
                          "Where it lives in its content root; changing it moves the file") };
}

bool FilePathProperty::BeginEdit(std::string_view key)
{
    return key == "path" && Path && !Path().empty();
}

PropertyEditResult FilePathProperty::CommitEdit(std::string_view key, std::string_view value)
{
    if (key != "path" || value.empty() || !Move)
        return PropertyEditResult::Refused;
    if (value == RootRelative(Path()))
        return PropertyEditResult::Unchanged;
    Move(std::string(value));
    return PropertyEditResult::Applied;
}
