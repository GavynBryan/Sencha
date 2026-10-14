#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

// A content file's picture as a document shows it: a host image, tinted by the
// colour when there is one, or the colour alone without an image; both empty
// for a file with no picture yet.
struct AssetPicture
{
    std::string Image = {};
    std::string Color = {};
};

// Where pictures of some kinds of content file come from.
class IAssetPictureSource
{
public:
    virtual ~IAssetPictureSource() = default;
    // The picture of the file at `assetPath`; asking for one marks it wanted.
    [[nodiscard]] virtual AssetPicture PictureOf(std::string_view assetPath) = 0;
    // Forgets what it worked out about files, which have changed on disk.
    virtual void Forget() {}
};

//=============================================================================
// AssetPictures
// Every picture source, by the extension of the files it pictures: the one place
// any file's picture is asked for. A provider serves while its registration lives.
//=============================================================================
class AssetPictures
{
public:
    class Registration
    {
    public:
        Registration() = default;
        Registration(AssetPictures& owner, std::string extension, IAssetPictureSource& source);
        Registration(Registration&& other) noexcept;
        Registration& operator=(Registration&& other) noexcept;
        Registration(const Registration&) = delete;
        Registration& operator=(const Registration&) = delete;
        ~Registration();

    private:
        void Release();

        AssetPictures* Owner = nullptr;
        std::string Extension;
        IAssetPictureSource* Source = nullptr;
    };

    AssetPictures() = default;
    AssetPictures(const AssetPictures&) = delete;
    AssetPictures& operator=(const AssetPictures&) = delete;

    // `source` pictures files ending in `extension` (".smat") until the
    // registration goes; a later registration for the same extension wins,
    // and an earlier one going leaves it be.
    [[nodiscard]] Registration Provide(std::string extension, IAssetPictureSource& source);
    [[nodiscard]] AssetPicture PictureOf(std::string_view assetPath);
    // Every source forgets what it worked out: files changed on disk.
    void Forget();

private:
    std::unordered_map<std::string, IAssetPictureSource*> Sources;
};
