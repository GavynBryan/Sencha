#pragma once

#include <anim/AnimTypes.h>
#include <ecs/ComponentAnnotations.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>

inline constexpr std::uint8_t kAnimNoSection = 0xFF;

enum class AnimFlowPhase : std::uint8_t
{
    None,
    Playing,
    // Holds its final pose and decides nothing more.
    Complete,
};

struct AnimLayerFlow
{
    AnimTick SectionStartTick = 0;
    // Excludes loops; what the authority stamps as the anchor's start.
    AnimTick SectionEnteredTick = 0;
    // The playing section by its tag: what a rebind finds it again by.
    GameplayTagId SectionTag;
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
