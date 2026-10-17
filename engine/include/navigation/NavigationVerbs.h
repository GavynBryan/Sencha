#pragma once

#include <authored/AuthoredQueryDispatcher.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>

#include <string_view>
#include <vector>

class World;

inline constexpr std::string_view kNavigationMoveToVerb = "navigation.move_to";
inline constexpr std::string_view kNavigationApproachVerb = "navigation.approach";
inline constexpr std::string_view kNavigationStopVerb = "navigation.stop";
inline constexpr std::string_view kNavigationPhaseQuery = "navigation.phase";
inline constexpr std::string_view kNavigationFailureQuery = "navigation.failure";
inline constexpr std::string_view kNavigationRemainingQuery = "navigation.remaining";

void DeclareNavigationVerbs(VerbRegistrationScope& scope);
void DeclareNavigationQueries(AuthoredQueryRegistrationScope& scope);

// The verbs give an agent its orders through RequestNavigation and
// CancelNavigation; what becomes of an order is read back with the queries.
class NavigationOperations
{
public:
    explicit NavigationOperations(World& world) : Entities(&world) {}

    VerbAdmission MoveTo(const VerbInvocation& invocation);
    VerbAdmission Approach(const VerbInvocation& invocation);
    VerbAdmission Stop(const VerbInvocation& invocation);

private:
    World* Entities;
};

inline VerbAdmission InvokeNavigationMoveTo(NavigationOperations& operations, const VerbInvocation& invocation)
{
    return operations.MoveTo(invocation);
}

inline VerbAdmission InvokeNavigationApproach(NavigationOperations& operations, const VerbInvocation& invocation)
{
    return operations.Approach(invocation);
}

inline VerbAdmission InvokeNavigationStop(NavigationOperations& operations, const VerbInvocation& invocation)
{
    return operations.Stop(invocation);
}

[[nodiscard]] std::vector<AuthoredQueryBindingToken> BindNavigationQueries(AuthoredQueryDispatcher& dispatcher,
                                                                           const World& world);
