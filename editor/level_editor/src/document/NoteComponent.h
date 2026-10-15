#pragma once

#include <core/metadata/EnumSchema.h>
#include <core/text/InlineString.h>
#include <ecs/ComponentAnnotations.h>

#include <array>
#include <cstdint>

enum class NoteState : std::uint8_t
{
    Open,
    Resolved,
};

template <>
struct EnumSchema<NoteState>
{
    static constexpr std::array Values = {
        EnumValue{ NoteState::Open, "open" },
        EnumValue{ NoteState::Resolved, "resolved" },
    };
};

// A remark left in the level for whoever works on it next, about the entity
// it is parented to, or about where it stands. Authoring only: the cook strips it.
struct SENCHA_COMPONENT("note")
       SENCHA_SCHEMA("note")
       SENCHA_SCENE_CHUNK("NOTE")
Note
{
    SENCHA_FIELD("text")
    InlineString<256> Text;

    SENCHA_FIELD("author")
    SENCHA_TOOLTIP("Who left it: a person, or the agent that proposed it.")
    InlineString<32> Author;

    SENCHA_FIELD("state")
    NoteState State = NoteState::Open;
};

#if !defined(SENCHA_CODEGEN)
#  include "document/NoteComponent.sencha.h"
#endif
