#include <authored/AuthoredClassification.h>

std::string_view AuthoredScopeName(AuthoredScope scope)
{
    switch (scope)
    {
    case AuthoredScope::Cosmetic: return "cosmetic";
    case AuthoredScope::Gameplay: return "gameplay";
    }
    return "unknown";
}
