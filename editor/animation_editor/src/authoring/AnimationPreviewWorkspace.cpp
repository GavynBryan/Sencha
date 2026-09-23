#include "authoring/AnimationPreviewWorkspace.h"

#include "authoring/AnimationEventBindings.h"

#include <anim/AnimBehaviorSet.h>
#include <anim/AnimFactSchema.h>
#include <anim/AnimRequestSchema.h>
#include <anim/AnimRigData.h>
#include <anim/AnimSelectorData.h>
#include <anim/AnimSlotMapData.h>
#include <anim/AnimationClipSampling.h>
#include <anim/SkinningPalette.h>
#include <assets/data/DataAssetSubtype.h>
#include <assets/runtime/RuntimeAssets.h>
#include <authored/VerbBindingData.h>

#include <algorithm>
#include <format>
#include <filesystem>

AnimationPreviewWorkspace::AnimationPreviewWorkspace(RuntimeAssets& assets, std::function<void(World&)> vocabulary)
    : Simulation(assets.DataAssets, &assets.AnimationClips, std::move(vocabulary), &assets.Skeletons)
    , Assets(assets)
{
    Material material;
    material.BaseColor = Vec4(0.65f, 0.7f, 0.8f, 1.0f);
    DefaultMaterial = Assets.Materials.Create(material);
    DefaultMaterialLease = AssetLease::Adopt(
        AssetType::Material, Assets.Materials, DefaultMaterial.ToToken());
    RefreshBrowser();
}

void AnimationPreviewWorkspace::RefreshBrowser()
{
    MeshPaths.clear();
    SkeletonPaths.clear();
    ClipPaths.clear();
    MaterialPaths.clear();
    RequestSchemaPaths.clear();
    RigPaths.clear();
    SelectorPaths.clear();
    BehaviorSetPaths.clear();
    SlotMapPaths.clear();
    FactSchemaPaths.clear();
    for (const auto& [path, record] : Assets.Registry.Records())
    {
        if (record.Type == AssetType::SkinnedMesh)
            MeshPaths.push_back(path);
        else if (record.Type == AssetType::Skeleton)
            SkeletonPaths.push_back(path);
        else if (record.Type == AssetType::AnimationClip)
            ClipPaths.push_back(path);
        else if (record.Type == AssetType::Material)
            MaterialPaths.push_back(path);
        else if (record.Type == AssetType::Data)
        {
            const std::string subtype = PeekDataAssetSubtype(Assets.Assets.DefaultSource(), record);
            if (subtype == kAnimRequestSchemaType)
                RequestSchemaPaths.push_back(path);
            else if (subtype == kAnimRigType)
                RigPaths.push_back(path);
            else if (subtype == kAnimSelectorType)
                SelectorPaths.push_back(path);
            else if (subtype == kAnimBehaviorSetType)
                BehaviorSetPaths.push_back(path);
            else if (subtype == kAnimSlotMapType)
                SlotMapPaths.push_back(path);
            else if (subtype == kAnimFactSchemaType)
                FactSchemaPaths.push_back(path);
        }
    }
    for (auto* paths : { &MeshPaths, &SkeletonPaths, &ClipPaths, &MaterialPaths, &RequestSchemaPaths,
                         &RigPaths, &SelectorPaths, &BehaviorSetPaths, &SlotMapPaths, &FactSchemaPaths })
        std::sort(paths->begin(), paths->end());
}

bool AnimationPreviewWorkspace::OpenAnimationDocument(const std::string& path)
{
    if (DataDocument* open = FindDocument(path))
    {
        for (std::size_t i = 0; i < Documents.size(); ++i)
            if (Documents[i].get() == open)
                SelectDocument(i);
        return true;
    }
    const auto* record = Assets.Registry.FindByPath(path);
    if (!record || record->Type != AssetType::Data)
    {
        DocumentError = "The asset is not registered in this project.";
        return false;
    }
    auto document = DataDocument::Open(record->FilePath, path, Assets.DataTypes,
                                      Assets.DataSchemas, &DocumentError);
    if (!document) return false;
    static constexpr std::string_view kEditable[] = { kAnimRequestSchemaType, kAnimRigType, kAnimSelectorType,
                                                      kAnimBehaviorSetType, kAnimSlotMapType, kAnimFactSchemaType,
                                                      kVerbBindingsTypeName };
    if (std::find(std::begin(kEditable), std::end(kEditable), document->Subtype()) == std::end(kEditable))
    {
        DocumentError = "Select an animation asset: a rig, schema, behavior set, selector, slot map or bindings.";
        return false;
    }
    Documents.push_back(std::move(document));
    SelectDocument(Documents.size() - 1);
    DocumentError.clear();
    return true;
}

DataDocument* AnimationPreviewWorkspace::FindDocument(std::string_view path)
{
    for (const auto& document : Documents)
        if (document->VirtualPath() == path)
            return document.get();
    return nullptr;
}

DataDocument* AnimationPreviewWorkspace::ActiveDocumentOf(std::string_view subtype)
{
    if (ActiveDocument >= Documents.size() || Documents[ActiveDocument]->Subtype() != subtype)
        return nullptr;
    return Documents[ActiveDocument].get();
}

void AnimationPreviewWorkspace::CommitDocumentEdit(DataDocument& document)
{
    document.CommitEdit();
    DocumentChanged(document);
}

void AnimationPreviewWorkspace::DocumentChanged(DataDocument& document)
{
    ValidateDocument(document);
    std::string status;
    if (!document.IsSemanticallyValid())
        status = "The working version has errors; the preview keeps the last valid version.";
    else if (ApplyDocumentToPreview(document, status))
        Simulation.Rebind();
    PreviewStatus[document.VirtualPath()] = std::move(status);
}

bool AnimationPreviewWorkspace::ApplyDocumentToPreview(DataDocument& document, std::string& status)
{
    // The preview's cache is this editor's own: replacing a value here is how
    // an edit reaches the simulation, and it never touches the file.
    const DataAssetHandle handle = Assets.DataAssets.Find(document.VirtualPath());
    if (!handle.IsValid())
    {
        status = "Not loaded by the open rig; nothing in the preview uses it yet.";
        return false;
    }
    const DataAssetTypeRegistration* type = Assets.DataTypes.Find(document.Subtype());
    const JsonValue* data = document.Data();
    if (type == nullptr || data == nullptr)
    {
        status = "Not an animation asset the preview understands.";
        return false;
    }
    DataAssetCompileResult compiled = type->Compile(*data);
    if (!compiled.IsValid())
    {
        status = "The working version does not compile (" + compiled.Error
            + "); the preview keeps the last valid version.";
        return false;
    }
    std::vector<AssetLease> dependencies;
    for (const AssetRef& dependency : compiled.Dependencies)
    {
        AssetLease lease = Assets.Assets.LoadLease(dependency.Path, dependency.Type);
        if (!lease)
        {
            status = "'" + dependency.Path + "' does not load; the preview keeps the last valid version.";
            return false;
        }
        dependencies.push_back(std::move(lease));
    }
    if (!Assets.DataAssets.ReloadInPlace(document.VirtualPath(), document.Subtype(), compiled.Value,
                                         std::move(dependencies)))
    {
        status = "The preview could not take the new version.";
        return false;
    }
    status = "The preview runs the working version.";
    return true;
}

bool AnimationPreviewWorkspace::OpenRig(const std::string& path)
{
    const auto* record = Assets.Registry.FindByPath(path);
    AssetLease lease = Assets.Assets.LoadLease(path, AssetType::Data);
    const DataAssetHandle handle =
        lease ? DataAssetHandle::FromToken(lease.OpaqueToken()) : DataAssetHandle{};
    const AnimRigData* rig = Assets.DataAssets.TryGet<AnimRigData>(handle, kAnimRigType);
    if (record == nullptr || rig == nullptr)
    {
        ScenarioError = "Select an animation.rig asset that loads.";
        return false;
    }
    // Another rig's layers are other layers.
    LayerDisplay = {};

    // The scenario lives beside the rig's source as an editor-only sidecar the
    // asset scanner does not register.
    std::filesystem::path sidecar(record->FilePath);
    sidecar.replace_extension(".sanimscenario");

    AnimationScenario scenario;
    ScenarioLoadProblems.clear();
    if (std::filesystem::exists(sidecar))
    {
        std::optional<AnimationScenario> loaded =
            LoadAnimationScenario(sidecar.string(), ScenarioLoadProblems);
        if (!loaded)
        {
            ScenarioError = "The saved scenario could not be read; see Problems.";
            return false;
        }
        scenario = std::move(*loaded);
        scenario.RigPath = path;
    }
    else
    {
        scenario.Name = sidecar.stem().string();
        scenario.RigPath = path;
        scenario.Participants = { "player" };
    }

    Simulation.Close();
    RigLease = std::move(lease);
    RigPath = path;
    ScenarioFile = sidecar.string();
    ScenarioError.clear();
    (void)Simulation.Open(std::move(scenario));
    ViewportSource = AnimationViewportSource::Simulation;
    Navigation = AnimationNavigation{};
    if (!rig->SkeletonPath.empty() && Session.SkeletonPath() != rig->SkeletonPath && MeshPath.empty())
        (void)SelectSkeleton(rig->SkeletonPath);
    return true;
}

bool AnimationPreviewWorkspace::SaveScenario()
{
    if (!Simulation.IsOpen() || ScenarioFile.empty())
        return false;
    if (!SaveAnimationScenario(Simulation.Scenario(), ScenarioFile, ScenarioError))
        return false;
    Simulation.MarkScenarioSaved();
    ScenarioError.clear();
    return true;
}

const DataAssetCache& AnimationPreviewWorkspace::DataCache() const
{
    return Assets.DataAssets;
}

bool AnimationPreviewWorkspace::ReloadScenario()
{
    return !RigPath.empty() && OpenRig(RigPath);
}

void AnimationPreviewWorkspace::SelectDocument(std::size_t index)
{
    if (index >= Documents.size() || index == ActiveDocument) return;
    CancelAuthoringEdit();
    ActiveDocument = index;
}

void AnimationPreviewWorkspace::CancelAuthoringEdit()
{
    if (ActiveDocument < Documents.size() && Documents[ActiveDocument]->IsEditing())
    {
        Documents[ActiveDocument]->CancelEdit();
        ValidateDocument(*Documents[ActiveDocument]);
    }
    // A marker mid-drag goes back where it was, and the preview with it.
    for (const auto& document : ClipEventDocuments)
    {
        if (!document->IsEditing())
            continue;
        document->CancelEdit();
        ClipEventsChanged(*document);
    }
}

const AnimationClipCache& AnimationPreviewWorkspace::Clips() const
{
    return Assets.AnimationClips;
}

bool AnimationPreviewWorkspace::OpenClipEvents(const std::string& clipPath)
{
    if (FindClipEvents(clipPath) != nullptr)
    {
        ActiveClipEvents = clipPath;
        return true;
    }
    const std::optional<MeshClipSource> source = MeshClipSourceOf(clipPath);
    const AssetRecord* record = Assets.Registry.FindByPath(clipPath);
    if (!source || record == nullptr)
    {
        DocumentError = "Events are authored on a clip cooked from a mesh source in this project.";
        return false;
    }
    // The cooked file sits under its content root's cooked directory; the
    // sidecar sits beside the source in that root.
    std::filesystem::path root;
    for (std::filesystem::path at(record->FilePath); at.has_parent_path() && at != at.parent_path();
         at = at.parent_path())
    {
        if (at.filename() == kCookedCacheDirName)
        {
            root = at.parent_path();
            break;
        }
    }
    if (root.empty())
    {
        DocumentError = std::format("'{}' was not cooked into a content root, so its source cannot be found.", clipPath);
        return false;
    }
    std::unique_ptr<AnimationClipEventsDocument> document = AnimationClipEventsDocument::Open(
        clipPath, root / (source->SourceRelPath + std::string(kImportSettingsSuffix)), &DocumentError);
    if (document == nullptr)
        return false;
    ClipEventDocuments.push_back(std::move(document));
    ActiveClipEvents = clipPath;
    DocumentError.clear();
    return true;
}

AnimationClipEventsDocument* AnimationPreviewWorkspace::FindClipEvents(std::string_view clipPath)
{
    for (const auto& document : ClipEventDocuments)
        if (document->ClipPath() == clipPath)
            return document.get();
    return nullptr;
}

void AnimationPreviewWorkspace::ClipEventsChanged(AnimationClipEventsDocument& document)
{
    std::string& status = PreviewStatus[document.ClipPath()];
    const AnimationClipHandle clip = Assets.AnimationClips.Find(document.ClipPath());
    const AnimationClipData* current = Assets.AnimationClips.Get(clip);
    if (current == nullptr)
    {
        status = "The clip is not loaded, so the preview cannot play these events.";
        return;
    }
    if (const std::vector<std::string> problems = document.Problems(); !problems.empty())
    {
        status = "The preview keeps the last valid events: " + problems.front();
        return;
    }
    // The preview's clip cache is this editor's own: replacing the clip here
    // is how the working events reach the rig binding, and nothing is written.
    AnimationClipData working = *current;
    working.Events = document.CookedOrder();
    if (!Assets.AnimationClips.ReloadInPlace(clip, std::move(working)))
    {
        status = "The preview's copy of the clip could not be replaced.";
        return;
    }
    Simulation.Rebind();
    status.clear();
}

bool AnimationPreviewWorkspace::SaveClipEvents(AnimationClipEventsDocument& document)
{
    if (!document.Save(&DocumentError))
        return false;
    DocumentError.clear();
    return true;
}

bool AnimationPreviewWorkspace::CreateBinding(const std::string& bindingsPath, const std::string& key,
                                              const std::string& verb)
{
    const VerbRegistry* verbs = Simulation.Verbs();
    const VerbDefinition* definition = verbs != nullptr ? verbs->Get(verbs->Find(verb)) : nullptr;
    if (definition == nullptr)
    {
        DocumentError = std::format("'{}' is not a verb the preview declares.", verb);
        return false;
    }
    if (!OpenAnimationDocument(bindingsPath))
        return false;
    DataDocument* document = FindDocument(bindingsPath);
    JsonValue root = document->CopyRoot();
    if (!AddAnimationBindingRecord(root, MakeAnimationBindingRecord(key, *definition)))
    {
        DocumentError = std::format("'{}' already declares a binding '{}'.", bindingsPath, key);
        return false;
    }
    document->ReplaceRoot(std::move(root));
    DocumentChanged(*document);
    return true;
}

void AnimationPreviewWorkspace::ValidateDocument(DataDocument& document)
{
    document.Validate(Assets.DataTypes, Assets.DataSchemas);
}

bool AnimationPreviewWorkspace::SaveDocument(DataDocument& document)
{
    document.CommitEdit();
    ValidateDocument(document);
    if (document.IsExternallyModified())
    {
        DocumentError = "File changed on disk. Resolve or reload it before saving; external edits were not overwritten.";
        return false;
    }
    DocumentError.clear();
    return document.Save(&DocumentError);
}

bool AnimationPreviewWorkspace::ReloadDocument(DataDocument& document)
{
    document.CancelEdit();
    if (document.IsDirty())
    {
        DocumentError = "Reload refused: undo or save local edits first.";
        return false;
    }
    DocumentError.clear();
    return document.Reload(Assets.DataTypes, Assets.DataSchemas, &DocumentError);
}

bool AnimationPreviewWorkspace::SetSkeletonContent(SkeletonHandle skeleton)
{
    const auto* value = Assets.Skeletons.Get(skeleton);
    if (value == nullptr)
    {
        Error = "The selected skeleton is not resident.";
        return false;
    }
    std::optional<AnimationClipData> clip;
    if (ClipLease)
    {
        const auto* current = Assets.AnimationClips.Get(
            AnimationClipHandle::FromToken(ClipLease.OpaqueToken()));
        if (current && current->SkeletonPath == Assets.Skeletons.GetName(skeleton))
            clip = *current;
    }
    if (!Session.SetContent(std::string(Assets.Skeletons.GetName(skeleton)), *value,
                            clip, Error))
        return false;
    if (!clip)
    {
        ClipLease.Reset();
        ClipPath.clear();
    }
    return true;
}

bool AnimationPreviewWorkspace::SelectMesh(const std::string& path)
{
    auto lease = Assets.Assets.LoadLease(path, AssetType::SkinnedMesh);
    if (!lease)
    {
        Error = "Could not load skinned mesh: " + path;
        return false;
    }
    const auto mesh = SkinnedMeshHandle::FromToken(lease.OpaqueToken());
    if (!SetSkeletonContent(Assets.SkinnedMeshes->GetSkeletonHandle(mesh)))
        return false;
    MeshLease = std::move(lease);
    SkeletonLease.Reset();
    MeshPath = path;
    return true;
}

bool AnimationPreviewWorkspace::SelectSkeleton(const std::string& path)
{
    auto lease = Assets.Assets.LoadLease(path, AssetType::Skeleton);
    if (!lease)
    {
        Error = "Could not load skeleton: " + path;
        return false;
    }
    if (!SetSkeletonContent(SkeletonHandle::FromToken(lease.OpaqueToken())))
        return false;
    SkeletonLease = std::move(lease);
    MeshLease.Reset();
    MeshPath.clear();
    return true;
}

bool AnimationPreviewWorkspace::SelectClip(const std::string& path)
{
    // Picking a clip by hand is auditioning it; the simulation keeps running
    // underneath, untouched, and the viewport switches back when asked.
    ViewportSource = AnimationViewportSource::Audition;
    if (path.empty())
    {
        if (Session.Skeleton().Joints.empty())
            return true;
        if (!Session.SetContent(Session.SkeletonPath(), Session.Skeleton(), std::nullopt, Error))
            return false;
        ClipLease.Reset();
        ClipPath.clear();
        return true;
    }
    auto lease = Assets.Assets.LoadLease(path, AssetType::AnimationClip);
    const auto* clip = lease ? Assets.AnimationClips.Get(
        AnimationClipHandle::FromToken(lease.OpaqueToken())) : nullptr;
    if (!clip)
    {
        Error = "Could not load animation clip: " + path;
        return false;
    }
    AssetLease skeletonLease;
    const SkeletonData* skeleton = &Session.Skeleton();
    std::string skeletonPath = Session.SkeletonPath();
    if (skeleton->Joints.empty())
    {
        skeletonLease = Assets.Assets.LoadLease(clip->SkeletonPath, AssetType::Skeleton);
        skeleton = skeletonLease ? Assets.Skeletons.Get(
            SkeletonHandle::FromToken(skeletonLease.OpaqueToken())) : nullptr;
        if (!skeleton)
        {
            Error = "Could not load the clip's skeleton: " + clip->SkeletonPath;
            return false;
        }
        skeletonPath = clip->SkeletonPath;
    }
    if (!Session.SetContent(skeletonPath, *skeleton, *clip, Error))
        return false;
    if (skeletonLease)
        SkeletonLease = std::move(skeletonLease);
    ClipLease = std::move(lease);
    ClipPath = path;
    return true;
}

bool AnimationPreviewWorkspace::SelectMaterial(const std::string& path)
{
    if (path.empty())
    {
        MaterialLease.Reset();
        MaterialPath.clear();
        Error.clear();
        return true;
    }
    auto lease = Assets.Assets.LoadLease(path, AssetType::Material);
    if (!lease)
    {
        Error = "Could not load material: " + path;
        return false;
    }
    MaterialLease = std::move(lease);
    MaterialPath = path;
    Error.clear();
    return true;
}

void AnimationPreviewWorkspace::Frame(double wallSeconds)
{
    Session.Advance(wallSeconds);
    Simulation.Advance(wallSeconds);
    Scene.Queue.Reset();
    Scene.Poses->Reset();
    Scene.Bounds = Aabb3d::Empty();
    if (!MeshLease)
        return;
    const auto mesh = SkinnedMeshHandle::FromToken(MeshLease.OpaqueToken());
    const auto* geometry = Assets.SkinnedMeshes->Get(mesh);
    if (!geometry)
        return;
    Scene.Bounds = geometry->LocalBounds;
    const auto& palette = ViewportPalette();
    // This viewport owns its pose cache and one instance. Scope is nonzero to
    // keep this editor identity distinct from runtime entity namespaces.
    const auto slot = Scene.Poses->AppendInstance(mesh, RenderEntityKey{ .Scope = 1, .Entity = {} },
                                                 static_cast<std::uint32_t>(palette.size()));
    const auto offset = Scene.Poses->Instances[slot].PaletteOffset;
    std::copy(palette.begin(), palette.end(), Scene.Poses->Palettes.begin() + offset);
    const auto material = MaterialLease
        ? MaterialHandle::FromToken(MaterialLease.OpaqueToken()) : DefaultMaterial;
    const auto* value = Assets.Materials.Get(material);
    if (!value)
        return;
    for (std::size_t section = 0; section < geometry->Sections.size(); ++section)
    {
        RenderQueueItem item;
        item.SkinnedMesh = mesh;
        item.Material = material;
        item.SectionIndex = static_cast<std::uint32_t>(section);
        item.WorldBounds = geometry->LocalBounds;
        item.PoseSlot = slot;
        item.Pipeline = SelectOpaquePipeline(*value);
        item.Pass = ResolveMaterialPass(*value);
        if (item.Pass == ShaderPassId::ForwardTransparent)
            Scene.Queue.AddTransparent(item);
        else
            Scene.Queue.AddOpaque(item);
    }
    Scene.Queue.SortOpaque();
}

const std::vector<Mat4>& AnimationPreviewWorkspace::ViewportPalette()
{
    if (ViewportSource != AnimationViewportSource::Simulation || !Simulation.IsOpen())
    {
        ViewportNote.clear();
        return Session.Palette();
    }
    // Every layer's resolved content at its content time, composed into
    // scratch: the audition's clip and clock are left as they were.
    const SkeletonData& skeleton = Session.Skeleton();
    const AnimBoundRig* rig = Simulation.Rig();
    const AnimContentState* content = Simulation.Content();
    ViewportNote.clear();
    if (rig == nullptr || content == nullptr || skeleton.Joints.empty())
    {
        BuildRestSkinningPalette(skeleton, SimulationPalette);
        return SimulationPalette;
    }
    const std::vector<AnimPoseLayer> layers = AnimationPreviewPoseLayers(
        *rig, *content, Simulation.Selection(), Assets.AnimationClips, LayerDisplay, Session.SkeletonPath(),
        ViewportNote);
    ComposeAnimPose(skeleton, layers, PoseScratch, SimulationLocal);
    BuildPosedModelTransforms(skeleton, SimulationLocal, SimulationModel);
    BuildSkinningPalette(skeleton, SimulationModel, SimulationPalette);
    return SimulationPalette;
}

const SkeletonData* AnimationPreviewWorkspace::RigSkeleton() const
{
    const AnimBoundRig* rig = Simulation.Rig();
    return rig != nullptr && rig->Skeleton.IsValid() ? Assets.Skeletons.Get(rig->Skeleton) : nullptr;
}

std::vector<AnimPoseLayer> AnimationPreviewPoseLayers(const AnimBoundRig& rig, const AnimContentState& content,
                                                      const AnimSelectorState* selection,
                                                      const AnimationClipCache& clips,
                                                      const AnimationLayerDisplay& display,
                                                      std::string_view skeletonPath, std::string& note)
{
    std::vector<AnimPoseLayer> layers;
    note.clear();
    const auto say = [&](std::string text) {
        if (!note.empty())
            note += "; ";
        note += std::move(text);
    };
    for (std::size_t l = 0; l < rig.Layers.size() && l < kAnimMaxLayers; ++l)
    {
        const AnimBoundLayer& bound = rig.Layers[l];
        const AnimLayerContent& layer = content.Layers[l];
        if (!display.Shows(l) || layer.Clip >= rig.Contents.size())
            continue;
        const AnimBoundContent& played = rig.Contents[layer.Clip];
        const AnimationClipData* clip = clips.Get(played.Clip);
        if (clip == nullptr)
            continue;
        if (clip->SkeletonPath != skeletonPath)
        {
            say(std::format("{}: {} animates another skeleton than the one on screen", bound.NameText, played.Path));
            continue;
        }
        AnimPoseLayer pose;
        pose.Clip = clip;
        pose.TimeSeconds = layer.TimeSeconds;
        pose.Weight = AnimLayerWeight(rig, l, selection);
        pose.Mode = bound.Mode;
        // A mask is over the rig's skeleton; on screen is the same one or the
        // clip would have been left out above.
        pose.Mask = bound.Mask;
        layers.push_back(pose);
        say(std::format("{}: {}", bound.NameText, played.Path));
    }
    return layers;
}
