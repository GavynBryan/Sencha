#include <assets/cook/UiPackageCook.h>

#include <assets/cook/UiSourceScan.h>
#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageSerializer.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace
{
constexpr std::size_t kMaxStyleSheetDepth = 16;

std::string_view DirectoryOf(std::string_view relPath)
{
    const std::size_t slash = relPath.find_last_of('/');
    return slash == std::string_view::npos ? std::string_view{} : relPath.substr(0, slash);
}

std::string_view FileNameOf(std::string_view relPath)
{
    const std::size_t slash = relPath.find_last_of('/');
    return slash == std::string_view::npos ? relPath : relPath.substr(slash + 1);
}

// Resolves `reference` as written against the file that wrote it, and hands
// back a path relative to the assets root. "../shared/base.rcss" from
// "ui/hud/panel.rcss" becomes "ui/shared/base.rcss".
std::string ResolveAgainst(std::string_view referrerRelPath, std::string_view reference)
{
    if (reference.empty())
        return {};
    // An assets-root-absolute spelling, which authors reach for once a project
    // has more than one UI directory.
    if (reference.front() == '/')
        return std::filesystem::path(reference.substr(1)).lexically_normal().generic_string();

    const std::string_view directory = DirectoryOf(referrerRelPath);
    std::filesystem::path combined = directory.empty()
        ? std::filesystem::path(reference)
        : std::filesystem::path(directory) / std::filesystem::path(reference);
    return combined.lexically_normal().generic_string();
}

std::string TextOf(std::span<const std::byte> bytes)
{
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

} // namespace

std::vector<std::string_view> UiPackageImporter::SourceExtensions() const
{
    return { ".rml" };
}

ImportResult UiPackageImporter::Import(const ImportInput& input, ICookOutputWriter& output)
{
    if (input.Bytes.empty())
        return ImportResult{ .Error = "ui package import: empty source" };

    const std::string rootRelPath(input.SourceRelPath);
    const std::string rootText = TextOf(input.Bytes);

    UiPackage package;
    // Named by filename alone: the document engine joins a relative reference
    // against the document's own path, so keeping the root at the package's
    // notional top level makes "theme.rcss" beside it resolve as written.
    package.RootDocumentName = std::string(FileNameOf(rootRelPath));

    UiPackageBlob rootBlob;
    rootBlob.VirtualName = package.RootDocumentName;
    rootBlob.SourcePath = rootRelPath;
    rootBlob.Kind = UiBlobKind::Document;
    rootBlob.Bytes.assign(input.Bytes.begin(), input.Bytes.end());
    package.Blobs.push_back(std::move(rootBlob));

    UiSourceReferences references;
    ScanUiSource(rootText, UiBlobKind::Document, rootRelPath, references);

    // The root document's own resources, resolved against it now so that by the
    // time the table is built every entry is assets-root-relative regardless of
    // which file named it.
    for (AssetRef& resource : references.Resources)
        resource.Path = ResolveAgainst(rootRelPath, resource.Path);

    // Breadth-first over the stylesheet graph, resolving each reference against
    // the file that made it. `pending` carries the referrer so a nested @import
    // resolves relative to the sheet that wrote it, not to the root document.
    // Two namespaces, resolved in step. The real path is where the bytes come
    // from on disk; the virtual name is what the document engine will ask for,
    // which is the reference joined against the referrer's own virtual name.
    struct PendingSheet
    {
        std::string ReferrerRelPath;
        std::string ReferrerVirtualName;
        std::string Reference;
        std::size_t Depth = 0;
    };

    std::vector<PendingSheet> pending;
    for (const std::string& sheet : references.StyleSheets)
        pending.push_back(PendingSheet{ rootRelPath, package.RootDocumentName, sheet, 1 });
    references.StyleSheets.clear();

    std::vector<std::string> additionalSources;
    std::vector<std::string> visited;

    for (std::size_t i = 0; i < pending.size(); ++i)
    {
        const PendingSheet sheet = pending[i];
        if (sheet.Depth > kMaxStyleSheetDepth)
        {
            return ImportResult{ .Error = std::format(
                "ui package import: stylesheet imports nested deeper than {} from '{}'; "
                "this is usually an import cycle", kMaxStyleSheetDepth, sheet.Reference) };
        }

        const std::string resolved = ResolveAgainst(sheet.ReferrerRelPath, sheet.Reference);
        if (resolved.empty())
        {
            return ImportResult{ .Error = std::format(
                "ui package import: '{}' references a stylesheet with no path", sheet.ReferrerRelPath) };
        }
        // A sheet imported from two places is packaged once. The cascade order
        // the document engine applies is its own business; what matters here is
        // that the bytes are present exactly once.
        if (std::find(visited.begin(), visited.end(), resolved) != visited.end())
            continue;
        visited.push_back(resolved);

        if (input.Sources == nullptr)
        {
            return ImportResult{ .Error = std::format(
                "ui package import: '{}' imports '{}', but this cook was given no way to read it",
                sheet.ReferrerRelPath, resolved) };
        }

        std::vector<std::byte> sheetBytes;
        if (!input.Sources->ReadSource(resolved, sheetBytes))
        {
            return ImportResult{ .Error = std::format(
                "ui package import: '{}' imports '{}', which does not exist",
                sheet.ReferrerRelPath, resolved) };
        }

        // Recorded whether or not it parsed into anything useful: the driver
        // hashes it, and an empty stylesheet that later gains content must
        // recook this document.
        additionalSources.push_back(resolved);

        const std::string sheetText = TextOf(sheetBytes);

        UiSourceReferences nested;
        ScanUiSource(sheetText, UiBlobKind::StyleSheet, resolved, nested);

        UiPackageBlob blob;
        // The name the referrer used, so the engine's own path join finds it.
        blob.VirtualName = NormalizeUiBlobName(
            ResolveAgainst(sheet.ReferrerVirtualName, sheet.Reference));
        blob.SourcePath = resolved;
        blob.Kind = UiBlobKind::StyleSheet;
        blob.Bytes = std::move(sheetBytes);
        package.Blobs.push_back(std::move(blob));

        const std::string resolvedVirtualName = NormalizeUiBlobName(
            ResolveAgainst(sheet.ReferrerVirtualName, sheet.Reference));
        for (const std::string& nestedSheet : nested.StyleSheets)
        {
            pending.push_back(PendingSheet{
                resolved, resolvedVirtualName, nestedSheet, sheet.Depth + 1 });
        }
        for (const AssetRef& resource : nested.Resources)
        {
            references.Resources.push_back(
                AssetRef{ resource.Type, ResolveAgainst(resolved, resource.Path) });
        }
        for (const UiUnsupportedFeature& note : nested.Unsupported)
            references.Unsupported.push_back(note);
    }

    // Resources are named relative to the file that referenced them, and become
    // asset paths. A texture or face is a real asset with its own identity, so
    // unlike a stylesheet it is referenced rather than copied in.
    for (const AssetRef& resource : references.Resources)
    {
        // Already assets-root-relative: a stylesheet's resources were resolved
        // against that sheet above, and the root document's against itself here.
        const std::string resolved = resource.Path.starts_with("asset://")
            ? resource.Path.substr(8)
            : resource.Path;
        if (resolved.empty())
            continue;
        const std::string assetPath = "asset://" + resolved;
        const bool already = std::any_of(package.Resources.begin(), package.Resources.end(),
            [&](const AssetRef& existing) {
                return existing.Type == resource.Type && existing.Path == assetPath;
            });
        if (!already)
            package.Resources.push_back(AssetRef{ resource.Type, assetPath });
    }

    package.Unsupported = std::move(references.Unsupported);

    std::vector<std::byte> suiBytes;
    if (!WriteSuiToBytes(package, suiBytes))
        return ImportResult{ .Error = "ui package import: sui serialization failed" };

    CookedArtifact artifact;
    artifact.Path = "asset://" + rootRelPath;
    artifact.FileRelPath = ".cooked/" + rootRelPath + ".sui";
    artifact.Type = AssetType::UiPackage;

    if (!output.WriteBytes(artifact.FileRelPath, suiBytes))
        return ImportResult{ .Error = "ui package import: artifact write failed" };

    ImportResult result;
    result.Artifacts.push_back(std::move(artifact));
    result.AdditionalSources = std::move(additionalSources);
    return result;
}
