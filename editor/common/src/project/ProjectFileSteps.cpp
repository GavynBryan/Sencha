#include "project/ProjectFileSteps.h"

#include "documents/DocumentSourceSet.h"
#include "project/AssetAdoption.h"
#include "project/ReferenceIndex.h"
#include "text/PathLeaf.h"

#include <core/assets/AssetPath.h>
#include <core/assets/AssetIdentity.h>
#include <core/json/JsonParser.h>
#include <zone/WorldPartitionManifest.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <utility>

namespace
{
constexpr std::string_view kStepPrefix = "file-step:";
constexpr std::string_view kTrashFolder = ".trash";
constexpr std::string_view kSceneSource = ".sscene";
constexpr std::string_view kWorld = ".sworld";

// Whether the string literal starting at `quote` is the value of a `path`
// member -- the hint inside an {id, path} reference -- rather than a reference.
bool IsPathMember(std::string_view text, std::size_t quote)
{
    std::size_t at = text.find_last_not_of(" \t\r\n", quote == 0 ? std::string_view::npos : quote - 1);
    if (at == std::string_view::npos || text[at] != ':')
        return false;
    at = text.find_last_not_of(" \t\r\n", at - 1);
    if (at == std::string_view::npos)
        return false;
    if (text[at] == '"' || text[at] == '\'')
        --at;
    constexpr std::string_view key = "path";
    return at + 1 >= key.size() && text.substr(at + 1 - key.size(), key.size()) == key;
}

std::string Reference(AssetId id, std::string_view path)
{
    return std::format("{{ \"id\": \"{}\", \"path\": \"{}\" }}", AssetIdToString(id), path);
}

// Whether the world in `world` names `scene` as one of its parts, by a path
// relative to itself rather than as a reference (pipeline Decision A).
bool WorldHasPart(const ProjectFile& world, const std::filesystem::path& scene)
{
    const std::optional<std::string> text = ReadProjectFileText(world.FilePath);
    const std::optional<JsonValue> root = text.has_value() ? JsonParse(*text) : std::nullopt;
    const std::optional<WorldPartitionManifest> manifest =
        root.has_value() ? ReadWorldPartitionManifest(*root, nullptr) : std::nullopt;
    if (!manifest.has_value())
        return false;
    std::error_code ec;
    const auto names = [&](const std::string& ref) {
        return !ref.empty() && std::filesystem::equivalent(world.FilePath.parent_path() / ref, scene, ec);
    };
    return names(manifest->WorldSceneRef)
           || std::ranges::any_of(manifest->Zones, [&](const ZoneHeader& zone) { return names(zone.SceneRef); });
}

bool WriteFileText(const std::filesystem::path& file, const std::optional<std::string>& text)
{
    std::error_code ec;
    if (!text.has_value())
    {
        std::filesystem::remove(file, ec);
        return !ec;
    }
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << *text;
    out.close();
    return out.good();
}
} // namespace

std::optional<std::string> NamingMovedAsset(std::string_view text, std::string_view from, std::string_view to,
                                            AssetId id)
{
    const bool scene = from.ends_with(kSceneSource);
    const std::string cookedFrom = scene ? CookedSceneOf(from) : std::string{};
    const std::string cookedTo = scene ? CookedSceneOf(to) : std::string{};

    std::string out;
    out.reserve(text.size());
    bool changed = false;
    std::size_t copied = 0;
    for (std::size_t at = 0; at < text.size(); ++at)
    {
        const char c = text[at];
        if (c == '/' && at + 1 < text.size() && (text[at + 1] == '/' || text[at + 1] == '*'))
        {
            const bool line = text[at + 1] == '/';
            const std::size_t end = text.find(line ? "\n" : "*/", at + 2);
            at = end == std::string_view::npos ? text.size() : end + (line ? 0 : 1);
            continue;
        }
        if (c != '"' && c != '\'')
            continue;
        std::size_t close = at + 1;
        while (close < text.size() && text[close] != c)
            close += text[close] == '\\' ? 2 : 1;
        if (close >= text.size())
            break;
        const std::string_view literal = text.substr(at + 1, close - at - 1);

        std::string named;
        if (literal == from)
            named = IsPathMember(text, at) ? std::format("\"{}\"", to) : Reference(id, to);
        else if (literal.starts_with(from) && literal.substr(from.size()).starts_with('#'))
        {
            const std::string_view part = literal.substr(from.size() + 1);
            const std::string path = std::string(to) + "#" + std::string(part);
            named = IsPathMember(text, at) ? std::format("\"{}\"", path) : Reference(AssetIdOfPart(id, part), path);
        }
        else if (scene && literal == cookedFrom)
            named = IsPathMember(text, at) ? std::format("\"{}\"", cookedTo) : Reference(id, cookedTo);

        if (!named.empty())
        {
            out.append(text.substr(copied, at - copied));
            out += named;
            copied = close + 1;
            changed = true;
        }
        at = close;
    }
    if (!changed)
        return std::nullopt;
    out.append(text.substr(copied));
    return out;
}

std::string CopyPath(std::string_view from, const std::function<bool(const std::string&)>& taken)
{
    const std::size_t leaf = from.rfind('/') + 1;
    const std::size_t dot = from.find('.', leaf);
    const std::string_view stem = from.substr(0, dot);
    const std::string_view extension = dot == std::string_view::npos ? std::string_view{} : from.substr(dot);
    std::string copy;
    for (int n = 1; copy.empty() || taken(copy); ++n)
        copy = std::format("{}{}{}", stem, n == 1 ? std::string(" copy") : std::format(" copy {}", n), extension);
    return copy;
}

ProjectFileSteps::ProjectFileSteps(std::vector<std::filesystem::path> roots, DocumentSourceSet& journal,
                                   ProjectFileHooks hooks)
    : Roots(std::move(roots))
    , Journal(journal)
    , Hooks(std::move(hooks))
{
    Journal.AddSource(*this);
}

ProjectFileSteps::~ProjectFileSteps()
{
    Journal.RemoveSource(*this);
}

std::string ProjectFileSteps::Refusal(std::string_view assetPath, OpenFiles open,
                                      std::optional<ProjectFile>& file) const
{
    file = FindProjectFile(Roots, assetPath);
    if (!file.has_value())
        return std::format("{} is not one of the project's files.", assetPath);
    if (open == OpenFiles::Refuse && Hooks.IsOpen && Hooks.IsOpen(*file))
        return std::format("{} is open; close it first.", PathLeaf(assetPath));
    if (file->Extension == kSceneSource)
        for (const ProjectFile& world : ScanProjectFiles(Roots))
            if (world.Extension == kWorld && WorldHasPart(world, file->FilePath))
                return std::format("{} is part of {}, and goes where the world goes.", PathLeaf(assetPath),
                                   PathLeaf(world.AssetPath));
    return {};
}

std::filesystem::path ProjectFileSteps::RootOf(const std::filesystem::path& file) const
{
    for (const std::filesystem::path& root : Roots)
    {
        std::error_code ec;
        const std::filesystem::path within = std::filesystem::relative(file, root, ec);
        if (!ec && !within.empty() && !within.generic_string().starts_with(".."))
            return root;
    }
    return Roots.empty() ? std::filesystem::path{} : Roots.front();
}

std::filesystem::path ProjectFileSteps::FileAt(const ProjectFile& source, std::string_view assetPath) const
{
    // A file moves within the root that holds it.
    return (RootOf(source.FilePath) / std::string(ContentRelativePath(assetPath)))
        .lexically_normal();
}

std::string ProjectFileSteps::Move(std::string_view assetPath, std::string_view toAssetPath)
{
    std::optional<ProjectFile> source;
    if (std::string refused = Refusal(assetPath, OpenFiles::Follow, source); !refused.empty())
        return refused;
    if (!toAssetPath.starts_with(kAssetPathPrefix) || toAssetPath == assetPath)
        return "Name it somewhere else in the project.";
    if (std::filesystem::path(std::string(toAssetPath)).extension().string() != source->FilePath.extension().string())
        return "A file keeps its kind: name it with the same extension.";
    const std::filesystem::path target = FileAt(*source, toAssetPath);
    std::error_code ec;
    if (std::filesystem::exists(target, ec))
        return std::format("{} already exists.", toAssetPath);
    // Its parts are named relative to it, and stay where they are.
    if (source->Extension == kWorld && target.parent_path() != source->FilePath.parent_path())
        return "A world is renamed where it is; its zones stay beside it.";

    Step step;
    step.Label = std::format("Move {} to {}", PathLeaf(assetPath), PathLeaf(toAssetPath));
    const bool asset = !Hooks.IsAsset || Hooks.IsAsset(*source);
    const AssetId id = AssetIdOf(*source);
    const std::filesystem::path statement = IdStatementOf(source->FilePath);
    if (asset && !source->StatedId.has_value())
    {
        // Stated where it is, before it moves, so the id goes with it.
        const std::optional<std::string> before = ReadProjectFileText(statement);
        step.Before.push_back({ statement, before, StatingAssetId(before.value_or(std::string{}), id) });
    }
    step.Renames.push_back({ source->FilePath, target });
    // A file that states its id beside itself has a sidecar by now: it goes too.
    if (asset && !StatesIdInside(source->Extension))
        step.Renames.push_back({ statement, SidecarOf(target) });
    const std::filesystem::path userState = source->FilePath.string() + std::string(kUserStateSuffix);
    if (std::filesystem::exists(userState, ec))
        step.Renames.push_back({ userState, target.string() + std::string(kUserStateSuffix) });

    for (const ProjectFile& document : asset ? ScanProjectFiles(Roots) : std::vector<ProjectFile>{})
    {
        if (!NamesOtherFiles(document.Extension) || document.AssetPath == assetPath)
            continue;
        const std::optional<std::string> text = ReadProjectFileText(document.FilePath);
        if (!text.has_value())
            continue;
        std::optional<std::string> named = NamingMovedAsset(*text, assetPath, toAssetPath, id);
        if (!named.has_value())
            continue;
        if (Hooks.KeepsNames && Hooks.KeepsNames(document))
            return std::format("{} names it and is open; close it first.", PathLeaf(document.AssetPath));
        step.After.push_back({ document.FilePath, text, std::move(named) });
    }
    const std::filesystem::path root = RootOf(source->FilePath);
    step.Changes = { { ProjectFileChange::Kind::Gone, std::string(assetPath), source->FilePath, root },
                     { ProjectFileChange::Kind::Appeared, std::string(toAssetPath), target, root } };
    step.Moved = MovedFile{ std::string(assetPath), std::string(toAssetPath), source->FilePath, target };
    Take(step, DocumentStep::Redo);
    Record(std::move(step));
    return {};
}

std::string ProjectFileSteps::Duplicate(std::string_view assetPath, std::string_view toAssetPath)
{
    std::optional<ProjectFile> source = FindProjectFile(Roots, assetPath);
    if (!source.has_value())
        return std::format("{} is not one of the project's files.", assetPath);
    if (!toAssetPath.starts_with(kAssetPathPrefix))
        return "Name it somewhere in the project.";
    const std::filesystem::path target = FileAt(*source, toAssetPath);
    std::error_code ec;
    if (std::filesystem::exists(target, ec))
        return std::format("{} already exists.", toAssetPath);

    Step step;
    step.Label = std::format("Duplicate {} as {}", PathLeaf(assetPath), PathLeaf(toAssetPath));
    step.Changes = { { ProjectFileChange::Kind::Appeared, std::string(toAssetPath), target, RootOf(source->FilePath) } };
    if (std::string refused = ArriveAsNew(step, source->FilePath, target, toAssetPath, !Hooks.IsAsset || Hooks.IsAsset(*source));
        !refused.empty())
        return refused;
    Take(step, DocumentStep::Redo);
    Record(std::move(step));
    return {};
}

std::string ProjectFileSteps::Import(const std::filesystem::path& external, std::string_view toAssetPath)
{
    if (Roots.empty() || !toAssetPath.starts_with(kAssetPathPrefix))
        return "Name it somewhere in the project.";
    ProjectFile arriving;
    arriving.AssetPath = std::string(toAssetPath);
    arriving.FilePath = (Roots.front() / std::string(ContentRelativePath(toAssetPath)))
                            .lexically_normal();
    arriving.Extension = ExtensionOf(arriving.FilePath);
    std::error_code ec;
    if (std::filesystem::exists(arriving.FilePath, ec))
        return std::format("{} already exists.", toAssetPath);

    Step step;
    step.Label = std::format("Import {}", PathLeaf(toAssetPath));
    step.Changes = { { ProjectFileChange::Kind::Appeared, std::string(toAssetPath), arriving.FilePath, Roots.front() } };
    if (std::string refused = ArriveAsNew(step, external, arriving.FilePath, toAssetPath,
                                          !Hooks.IsAsset || Hooks.IsAsset(arriving));
        !refused.empty())
        return refused;
    Take(step, DocumentStep::Redo);
    Record(std::move(step));
    return {};
}

std::string ProjectFileSteps::ArriveAsNew(Step& step, const std::filesystem::path& from,
                                          const std::filesystem::path& target, std::string_view toAssetPath,
                                          bool asset) const
{
    const std::optional<std::string> bytes = ReadProjectFileText(from);
    if (!bytes.has_value())
        return std::format("{} could not be read.", PathLeaf(from.generic_string()));
    if (!asset)
    {
        step.After.push_back({ target, std::nullopt, bytes });
        return {};
    }
    // A new asset: it states the id it is born with, never one it came with.
    const AssetId id = AssetIdOfPath(toAssetPath);
    const std::string extension = ExtensionOf(target);
    const auto stated = [&](const std::string& text) {
        return StatingAssetId(WithoutStatedId(text, extension == kSceneSource).value_or(text), id);
    };
    if (StatesIdInside(extension))
    {
        const std::optional<std::string> copy = stated(*bytes);
        if (!copy.has_value())
            return std::format("{} is not a document that can be copied.", PathLeaf(from.generic_string()));
        step.After.push_back({ target, std::nullopt, copy });
        return {};
    }
    step.After.push_back({ target, std::nullopt, bytes });
    const std::optional<std::string> sidecar = ReadProjectFileText(SidecarOf(from));
    step.After.push_back({ SidecarOf(target), std::nullopt, stated(sidecar.value_or(std::string{})) });
    return {};
}

std::string ProjectFileSteps::Trash(std::string_view assetPath)
{
    std::optional<ProjectFile> source;
    if (std::string refused = Refusal(assetPath, OpenFiles::Refuse, source); !refused.empty())
        return refused;
    const std::filesystem::path root = RootOf(source->FilePath);
    const std::filesystem::path relative = std::filesystem::relative(source->FilePath, root);
    // One folder a step, so two trashings of one name never meet.
    const std::filesystem::path trashed = root / kTrashFolder / std::to_string(Steps.size()) / relative;

    Step step;
    step.Label = std::format("Trash {}", PathLeaf(assetPath));
    step.Renames.push_back({ source->FilePath, trashed });
    std::error_code ec;
    if (std::filesystem::exists(SidecarOf(source->FilePath), ec))
        step.Renames.push_back({ SidecarOf(source->FilePath), SidecarOf(trashed) });
    step.Changes = { { ProjectFileChange::Kind::Gone, std::string(assetPath), source->FilePath, root } };
    Take(step, DocumentStep::Redo);
    Record(std::move(step));
    return {};
}

void ProjectFileSteps::Take(const Step& step, DocumentStep direction)
{
    const bool redo = direction == DocumentStep::Redo;
    // A document open in a rewritten file says the same as before it: it
    // takes the new bytes as read.
    const auto write = [&](const std::vector<Rewrite>& rewrites) {
        if (redo)
            for (const Rewrite& rewrite : rewrites)
            {
                (void)WriteFileText(rewrite.File, rewrite.After);
                Journal.NamesRewritten(rewrite.File);
            }
        else
            for (auto it = rewrites.rbegin(); it != rewrites.rend(); ++it)
            {
                (void)WriteFileText(it->File, it->Before);
                Journal.NamesRewritten(it->File);
            }
    };
    const auto rename = [&] {
        const auto move = [](const std::filesystem::path& from, const std::filesystem::path& to) {
            std::error_code ec;
            std::filesystem::create_directories(to.parent_path(), ec);
            std::filesystem::rename(from, to, ec);
        };
        if (redo)
            for (const Rename& each : step.Renames)
                move(each.From, each.To);
        else
            for (auto it = step.Renames.rbegin(); it != step.Renames.rend(); ++it)
                move(it->To, it->From);
    };

    const auto follow = [&] {
        if (step.Moved.has_value())
            Journal.FileMoved(redo ? *step.Moved
                                   : MovedFile{ step.Moved->ToAsset, step.Moved->FromAsset, step.Moved->To,
                                                step.Moved->From });
    };
    if (redo)
    {
        write(step.Before);
        rename();
        follow();
        write(step.After);
    }
    else
    {
        write(step.After);
        rename();
        follow();
        write(step.Before);
    }
    ++Taken;
    if (Hooks.Changed)
    {
        if (redo)
            for (const ProjectFileChange& change : step.Changes)
                Hooks.Changed(change);
        else
            for (auto it = step.Changes.rbegin(); it != step.Changes.rend(); ++it)
            {
                ProjectFileChange undone = *it;
                undone.What = undone.What == ProjectFileChange::Kind::Gone ? ProjectFileChange::Kind::Appeared
                                                                            : ProjectFileChange::Kind::Gone;
                Hooks.Changed(undone);
            }
    }
}

void ProjectFileSteps::Record(Step step)
{
    Steps.push_back(std::move(step));
    Journal.Record(DocumentRef{ this, std::string(kStepPrefix) + std::to_string(Steps.size() - 1) });
}

void ProjectFileSteps::StepDocument(std::string_view key, DocumentStep step)
{
    if (!key.starts_with(kStepPrefix))
        return;
    const std::size_t index = std::stoul(std::string(key.substr(kStepPrefix.size())));
    if (index < Steps.size())
        Take(Steps[index], step);
}

std::string ProjectFileSteps::DocumentLabel(std::string_view key) const
{
    if (!key.starts_with(kStepPrefix))
        return std::string(key);
    const std::size_t index = std::stoul(std::string(key.substr(kStepPrefix.size())));
    return index < Steps.size() ? Steps[index].Label : std::string(key);
}
