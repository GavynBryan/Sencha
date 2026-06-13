#include <core/assets/AssetPath.h>

bool IsValidAssetPath(std::string_view path)
{
    return path.starts_with(kAssetPathPrefix)
        && path.size() > kAssetPathPrefix.size()
        && path.find('\\') == std::string_view::npos;
}
