#include <assets/font/FontFaceAssetLoader.h>

#include <assets/font/FontFaceFormat.h>
#include <assets/font/FontFaceSerializer.h>
#include <core/logging/LoggingProvider.h>

#include <format>
#include <utility>

FontFaceAssetLoader::FontFaceAssetLoader(LoggingProvider& logging, FontFaceCache* cache)
    : Log(logging.GetLogger<FontFaceAssetLoader>())
    , Cache(cache)
{
}

AssetStaging FontFaceAssetLoader::LoadStaged(const AssetRecord& record, IAssetSource& source)
{
    AssetStaging staging;
    staging.Record = record;

    std::vector<std::byte> bytes;
    if (!ReadAssetBytes(source, record, bytes))
    {
        staging.Error = std::format("could not read font source for '{}'", record.Path);
        return staging;
    }

    // Sniff the container rather than trusting the extension: a cooked artifact
    // keeps its source's virtual path, so the path may say ".ttf" while the
    // bytes are a cooked .sfont. Raw face bytes are deliberately NOT accepted
    // here -- a face carries family, style and weight it cannot be asked to
    // guess, and the cook is where those are decided.
    if (!LooksLikeSfont(bytes.data(), bytes.size()))
    {
        staging.Error = std::format(
            "'{}' is not a cooked .sfont; fonts reach the runtime through the cook, "
            "which is where family, style and weight are decided", record.Path);
        return staging;
    }

    FontFace face;
    std::string parseError;
    if (!LoadSfontFromBytes(bytes, face, &parseError))
    {
        staging.Error = std::format("failed to parse .sfont for '{}': {}",
                                    record.Path, parseError);
        return staging;
    }

    staging.Payload = std::move(face);
    return staging;
}

FontFaceHandle FontFaceAssetLoader::CommitTyped(AssetStaging&& staged)
{
    if (Cache == nullptr)
    {
        Log.Error("FontFaceAssetLoader: no cache to commit '{}' into", staged.Record.Path);
        return {};
    }
    if (!staged.IsValid())
    {
        Log.Error("FontFaceAssetLoader: refusing to commit failed staging for '{}': {}",
                  staged.Record.Path, staged.Error);
        return {};
    }

    auto* face = std::any_cast<FontFace>(&staged.Payload);
    if (face == nullptr)
    {
        Log.Error("FontFaceAssetLoader: staging for '{}' carries the wrong payload type",
                  staged.Record.Path);
        return {};
    }

    return Cache->Register(staged.Record.Path, std::move(*face));
}
