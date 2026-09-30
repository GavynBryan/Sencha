#pragma once

#include <cstdint>

//=============================================================================
// BrushWorkCounters — per-frame tallies of brush geometry work, incremented at
// the producers so an architectural regression is visible as a number.
//
// Interactive counters measure reconstruction that retained facts exist to
// avoid: on an idle frame of an unchanged scene every one of them is zero, and
// an edit to one entity moves them by that entity's share only. Intentional
// counters measure the one-shot flattening whose product is the flattened
// geometry (cook, export, merge, bake); they are expected and reported apart.
//
// One process-wide frame tally, reset by whoever owns the frame (the render
// feature; a test resets it itself). Not thread-safe by design: every producer
// runs on the editor's main thread.
//=============================================================================
struct BrushWorkCounters
{
    // Interactive
    std::uint32_t Evaluations = 0;        // EvaluateBrushModifiers
    std::uint32_t PlacementRebuilds = 0;  // BrushPlacementFacts records rebuilt
    std::uint32_t ElementBuilds = 0;      // MeshElements::Faces/Edges/Vertices lists built
    std::uint32_t DrawRecordRebuilds = 0; // BrushDrawSet records rebuilt
    std::uint32_t Bakes = 0;              // BrushBakeCache GPU bakes
    std::uint32_t PieceWalks = 0;         // per-piece walks (ForEachVisibleBrushPiece, ForEachPieceWorldBounds)
    // Intentional flattening
    std::uint32_t CookCollects = 0;
    std::uint32_t ExportFlattens = 0;
    std::uint32_t MergeFlattens = 0;
    std::uint32_t BakeFlattens = 0;

    [[nodiscard]] static BrushWorkCounters& Frame();
    // The tally so far, then zero. The render feature calls this once per frame
    // and keeps the snapshot for the console readout.
    [[nodiscard]] static BrushWorkCounters TakeFrame();
    // The snapshot the last TakeFrame produced: what a readout reports.
    [[nodiscard]] static const BrushWorkCounters& LastFrame();

    [[nodiscard]] bool InteractiveIdle() const
    {
        return Evaluations == 0 && PlacementRebuilds == 0 && ElementBuilds == 0
            && DrawRecordRebuilds == 0 && Bakes == 0 && PieceWalks == 0;
    }
};
