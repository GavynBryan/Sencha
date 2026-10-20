#pragma once

template <typename Component>
struct SceneComponentValidation
{
    template <typename Archive>
    static bool Validate(const Component&, Archive&) { return true; }
};
