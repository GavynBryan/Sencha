#pragma once

#include "brush/CarveShape.h"
#include "document/LevelConventions.h"

#include <core/metadata/EnumSchema.h>
#include <ecs/ComponentAnnotations.h>

#include <array>
#include <cstdint>

// The spatial model: docs/plans/level-spatial-model.md, section 2. All three
// are authoring facts; the level cook strips them.

// A named region of air, "the hallway": a volume brush carrying this tag. Its
// name is the entity's name.
struct SENCHA_COMPONENT("space")
       SENCHA_SCHEMA("space")
       SENCHA_SCENE_CHUNK("SPAC")
Space
{
};

// Walls built around the volume, one slab per face outward by Thickness, as
// wall brushes the space parents (GeneratedWall).
struct SENCHA_COMPONENT("shell")
       SENCHA_SCHEMA("shell")
       SENCHA_SCENE_CHUNK("SHEL")
Shell
{
    SENCHA_FIELD("thickness")
    SENCHA_LABEL("Wall thickness")
    SENCHA_TOOLTIP("How far the generated walls extend outward from the space, in metres.")
    float Thickness = static_cast<float>(kDefaultShellThickness);
};

template <>
struct EnumSchema<CarveShape>
{
    static constexpr std::array Values = {
        EnumValue{ CarveShape::Rectangle, "rectangle" },
        EnumValue{ CarveShape::Arch, "arch" },
    };
};

// A cut through generated walls. Its transform is the opening's frame: the
// origin at the middle of its sill, +Y up, +Z through the wall.
struct SENCHA_COMPONENT("opening")
       SENCHA_SCHEMA("opening")
       SENCHA_SCENE_CHUNK("OPEN")
Opening
{
    SENCHA_FIELD("width")
    SENCHA_TOOLTIP("Metres across.")
    float Width = static_cast<float>(kDefaultOpeningWidth);

    SENCHA_FIELD("height")
    SENCHA_TOOLTIP("Metres from the sill to the top.")
    float Height = static_cast<float>(kDefaultOpeningHeight);

    SENCHA_FIELD("shape")
    CarveShape Shape = CarveShape::Rectangle;

    SENCHA_FIELD("arch_rise")
    SENCHA_LABEL("Arch rise")
    SENCHA_TOOLTIP("The fraction of the height the arch springs over, 0 to 1. Arches only.")
    float ArchRise = CarveShapeParams{}.ArchRise;
};

enum class WallState : std::uint8_t
{
    Generated,
    Kept,
};

template <>
struct EnumSchema<WallState>
{
    static constexpr std::array Values = {
        EnumValue{ WallState::Generated, "generated" },
        EnumValue{ WallState::Kept, "kept" },
    };
};

// A wall brush its parent space built (document/SpaceWalls.h). A kept wall
// was reshaped by hand and is no longer rebuilt.
struct SENCHA_COMPONENT("generated_wall")
       SENCHA_SCHEMA("generated_wall")
       SENCHA_SCENE_CHUNK("GWAL")
GeneratedWall
{
    SENCHA_FIELD("slot")
    SENCHA_TOOLTIP("Which face of the space, which piece of it, and which side.")
    std::uint32_t Slot = 0;

    SENCHA_FIELD("built")
    SENCHA_TOOLTIP("The wall's shape and placement as built; any other means it was reshaped by hand.")
    std::uint32_t Built = 0;

    SENCHA_FIELD("state")
    WallState State = WallState::Generated;
};

#if !defined(SENCHA_CODEGEN)
#  include "document/SpaceComponents.sencha.h"
#endif
