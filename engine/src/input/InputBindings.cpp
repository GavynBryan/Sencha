#include <input/InputBindings.h>

std::uint32_t BoundInputProfile::FindContext(std::string_view name) const
{
    for (std::size_t i = 0; i < Contexts.size(); ++i)
    {
        if (Contexts[i].Name == name)
            return static_cast<std::uint32_t>(i);
    }
    return kInvalidInputContext;
}
