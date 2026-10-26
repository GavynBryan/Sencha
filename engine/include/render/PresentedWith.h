#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>

#include <type_traits>

// On an entity drawn as part of another (a rifle on a body): a camera that
// excludes the other entity excludes this one too. Runtime only.
struct SENCHA_COMPONENT("sencha.presented_with") PresentedWith
{
    EntityId Root;
};

static_assert(std::is_trivially_copyable_v<PresentedWith>,
              "PresentedWith must be trivially copyable to live in ECS chunks");

#if !defined(SENCHA_CODEGEN)
#  include <render/PresentedWith.sencha.h>
#endif
