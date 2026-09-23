#pragma once

#include <anim/AnimTypes.h>
#include <ecs/ComponentAnnotations.h>

#include <cstdint>

//=============================================================================
// AnimFlowState
//
// Where each layer's flow is: the section, the tick it began, how many times it
// has looped, and whether it has finished. That is all a flow remembers;
// which flow is playing,
// and the clip its section plays, are the layer's content. A layer playing a
// clip leaves its entry unused.
//=============================================================================

inline constexpr std::uint8_t kAnimNoSection = 0xFF;

enum class AnimFlowPhase : std::uint8_t
{
    // The layer is not playing a flow.
    None,
    Playing,
    // Past its last section, or its cancel section: it holds its final pose
    // and decides nothing more, whatever its conditions do next.
    Complete,
};

struct AnimLayerFlow
{
    AnimTick SectionStartTick = 0;
    std::uint16_t LoopCount = 0;
    std::uint8_t Section = kAnimNoSection;
    AnimFlowPhase Phase = AnimFlowPhase::None;
};

struct SENCHA_COMPONENT("sencha.anim_flow_state") AnimFlowState
{
    AnimLayerFlow Layers[kAnimMaxLayers] = {};
};

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimFlowState.sencha.h>
#endif
