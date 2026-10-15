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

std::string_view AuthoredPredictionName(AuthoredPrediction prediction)
{
    switch (prediction)
    {
    case AuthoredPrediction::AuthorityOnly: return "authority_only";
    case AuthoredPrediction::Presentation: return "presentation";
    case AuthoredPrediction::Simulation: return "simulation";
    }
    return "authority_only";
}
