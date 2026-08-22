#include <render/ProjectedShadowRenderFeature.h>

#include <render/static_mesh/StaticMeshVertex.h>

#include <profiling/RenderInstrumentation.h>
#include <profiling/RenderStats.h>

ProjectedShadowRenderFeature::ProjectedShadowRenderFeature(
    ProjectedShadowSet& casters,
    const RenderLightSet& lights,
    const RenderQueue& queue,
    const CameraRenderData& camera,
    StaticMeshCache& meshes,
    const SkinnedMeshCache& skinnedMeshes,
    const ProjectedShadowBudgets& budgets,
    std::shared_ptr<ProjectedShadowFrameData> output)
    : Casters(&casters)
    , Lights(&lights)
    , Queue(&queue)
    , Camera(&camera)
    , Meshes(&meshes)
    , SkinnedMeshes(&skinnedMeshes)
    , Budgets(&budgets)
    , Output(std::move(output))
{
}

bool ProjectedShadowRenderFeature::Setup(const RendererServices& services)
{
    Instrumentation = services.Instrumentation;
    Silhouettes.Setup(services, sizeof(StaticMeshVertex));
    return true;
}

void ProjectedShadowRenderFeature::Teardown()
{
    Silhouettes.Teardown();
}

void ProjectedShadowRenderFeature::OnDraw(const FrameContext& frame)
{
    Output->Reset();
    RenderStats* stats = Instrumentation != nullptr ? Instrumentation->Stats : nullptr;
    if (stats != nullptr)
    {
        stats->ProjectedCastersGathered =
            static_cast<std::uint32_t>(Casters->Casters.size());
        stats->ProjectedCastersRendered = 0;
        stats->ProjectedCastersDropped = 0;
        stats->ProjectedReceiverDraws = 0;
    }
    if (!Lights->ProjectedShadowsEnabled || Casters->Casters.empty())
        return;

    const std::uint32_t dropped =
        RankAndClampProjectedCasters(*Casters, Camera->Position, Budgets->MaxCasters);
    if (stats != nullptr)
        stats->ProjectedCastersDropped = dropped;
    const ProjectedShadowTileGrid grid =
        MakeProjectedShadowTileGrid(Budgets->MaxCasters, Budgets->TilePixels);

    // One pass per caster builds the silhouette draw and its projection
    // record together, so a skipped caster (unresident mesh, masked-out
    // sections, off-screen, no receivers) can never skew a later caster's
    // tile or view-projection: the tile ordinal IS the position in
    // CasterDraws, which is the ordinal the silhouette pass renders. The
    // atlas bindless index is not known until the silhouette pass has run,
    // so it is stamped into the collected uniforms afterwards.
    CasterDraws.clear();
    SectionDraws.clear();
    Output->VertexStride = sizeof(StaticMeshVertex);
    for (const ProjectedShadowCaster& caster : Casters->Casters)
    {
        const GpuStaticMesh* mesh = SkinnedMeshes->Get(caster.Mesh);
        if (mesh == nullptr)
            continue;

        const Mat4 viewProjection =
            MakeProjectedShadowViewProjection(caster, Budgets->MaxDistance);

        ProjectedSilhouetteCasterDraw draw;
        draw.Mvp = viewProjection * caster.WorldMatrix;
        draw.FirstSection = static_cast<std::uint32_t>(SectionDraws.size());
        for (std::uint32_t section = 0; section < mesh->Sections.size(); ++section)
        {
            if ((caster.SectionMask & (1u << section)) == 0)
                continue;
            SectionDraws.push_back(ProjectedSilhouetteSectionDraw{
                .Vertex = mesh->VertexBuffer,
                .Index = mesh->IndexBuffer,
                .IndexCount = mesh->Sections[section].IndexCount,
                .IndexOffset = mesh->Sections[section].IndexOffset,
            });
        }
        draw.SectionCount =
            static_cast<std::uint32_t>(SectionDraws.size()) - draw.FirstSection;
        if (draw.SectionCount == 0)
            continue;
        const std::uint32_t thisTile =
            static_cast<std::uint32_t>(CasterDraws.size());
        CasterDraws.push_back(draw);

        // Projection record, fully baked for the game's one view: camera
        // matrix and scissor included, so the mesh feature only records.
        const Aabb3d swept =
            ProjectedShadowSweptBounds(caster, Budgets->MaxDistance);
        const ProjectedShadowScreenRect rect = ComputeProjectedShadowScreenRect(
            swept, Camera->ViewProjection,
            frame.TargetExtent.width, frame.TargetExtent.height);
        if (rect.Width == 0 || rect.Height == 0)
            continue;

        ReceiverIndices.clear();
        GatherProjectedShadowReceivers(Queue->Opaque(), swept,
                                       Budgets->MaxReceiversPerCaster,
                                       ReceiverIndices);
        if (ReceiverIndices.empty())
            continue;

        ProjectedShadowProjection projection;
        projection.Uniform.CameraViewProjection = Camera->ViewProjection;
        projection.Uniform.ShadowViewProjection = viewProjection;
        projection.Uniform.TileScaleBias =
            ProjectedShadowTileUvScaleBias(grid, thisTile);
        projection.Uniform.Params = Vec4(Lights->ProjectedShadowDarkness,
                                         Lights->ProjectedShadowFadeStart,
                                         0.0f, 0.0f);
        projection.ScissorX = rect.X;
        projection.ScissorY = rect.Y;
        projection.ScissorWidth = rect.Width;
        projection.ScissorHeight = rect.Height;
        projection.FirstReceiver =
            static_cast<std::uint32_t>(Output->Receivers.size());

        for (const std::uint32_t itemIndex : ReceiverIndices)
        {
            const RenderQueueItem& item = Queue->Opaque()[itemIndex];
            const GpuStaticMesh* receiverMesh = Meshes->Get(item.Mesh);
            if (receiverMesh == nullptr
                || item.SectionIndex >= receiverMesh->Sections.size())
                continue;
            const StaticMeshSection& section =
                receiverMesh->Sections[item.SectionIndex];
            Output->Receivers.push_back(ProjectedReceiverDraw{
                .Vertex = receiverMesh->VertexBuffer,
                .Index = receiverMesh->IndexBuffer,
                .IndexCount = section.IndexCount,
                .IndexOffset = section.IndexOffset,
                .World = item.WorldMatrix,
            });
        }
        projection.ReceiverCount =
            static_cast<std::uint32_t>(Output->Receivers.size())
            - projection.FirstReceiver;
        if (projection.ReceiverCount > 0)
            Output->Casters.push_back(projection);
    }
    if (CasterDraws.empty())
    {
        Output->Reset();
        return;
    }

    ProjectedSilhouetteInput silhouettes;
    silhouettes.TilesPerRow = grid.TilesPerRow;
    silhouettes.TilePixels = grid.TilePixels;
    silhouettes.Casters = CasterDraws;
    silhouettes.Sections = SectionDraws;
    if (!Silhouettes.Draw(frame, silhouettes))
    {
        // The projections sample an atlas that never rendered this frame.
        Output->Reset();
        return;
    }
    const float atlasIndex =
        static_cast<float>(Silhouettes.AtlasBindlessIndex());
    for (ProjectedShadowProjection& projection : Output->Casters)
        projection.Uniform.Params.Z = atlasIndex;

    Output->Ready = !Output->Casters.empty();
    if (stats != nullptr)
    {
        stats->ProjectedCastersRendered =
            static_cast<std::uint32_t>(Output->Casters.size());
        stats->ProjectedReceiverDraws =
            static_cast<std::uint32_t>(Output->Receivers.size());
    }
}
