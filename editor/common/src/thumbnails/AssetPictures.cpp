#include "thumbnails/AssetPictures.h"

#include <algorithm>
#include <cctype>
#include <utility>

AssetPictures::Registration::Registration(AssetPictures& owner, std::string extension, IAssetPictureSource& source)
    : Owner(&owner)
    , Extension(std::move(extension))
    , Source(&source)
{
}

AssetPictures::Registration::Registration(Registration&& other) noexcept
    : Owner(std::exchange(other.Owner, nullptr))
    , Extension(std::move(other.Extension))
    , Source(std::exchange(other.Source, nullptr))
{
}

AssetPictures::Registration& AssetPictures::Registration::operator=(Registration&& other) noexcept
{
    if (this != &other)
    {
        Release();
        Owner = std::exchange(other.Owner, nullptr);
        Extension = std::move(other.Extension);
        Source = std::exchange(other.Source, nullptr);
    }
    return *this;
}

AssetPictures::Registration::~Registration()
{
    Release();
}

void AssetPictures::Registration::Release()
{
    if (Owner == nullptr)
        return;
    const auto registered = Owner->Sources.find(Extension);
    if (registered != Owner->Sources.end() && registered->second == Source)
        Owner->Sources.erase(registered);
    Owner = nullptr;
}

AssetPictures::Registration AssetPictures::Provide(std::string extension, IAssetPictureSource& source)
{
    Sources[extension] = &source;
    return Registration(*this, std::move(extension), source);
}

AssetPicture AssetPictures::PictureOf(std::string_view assetPath)
{
    const std::size_t dot = assetPath.rfind('.');
    if (dot == std::string_view::npos)
        return {};
    std::string extension(assetPath.substr(dot));
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto source = Sources.find(extension);
    return source != Sources.end() ? source->second->PictureOf(assetPath) : AssetPicture{};
}

void AssetPictures::Forget()
{
    for (const auto& [extension, source] : Sources)
        source->Forget();
}
