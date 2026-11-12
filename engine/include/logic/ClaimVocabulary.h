#pragma once

#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbDispatcher.h>

#include <string_view>
#include <vector>

class World;

// Holds a place in a group's claim for as long as the lease is held:
// {group, claim, count = 1, share = 0}. Refused when every place is taken.
// Authority only.
inline constexpr std::string_view kClaimsHoldLease = "claims.hold";
// {group, claim}: how many places are held.
inline constexpr std::string_view kClaimsCountQuery = "claims.count";
// {holder, group, claim}: whether that entity holds a place.
inline constexpr std::string_view kClaimsHeldQuery = "claims.held";

// The claim lease and queries, declared into the World's catalogs with a
// ClaimBoard. Idempotent.
bool InstallClaimVocabulary(World& world);

// The claims.hold operation over the World's ClaimBoard.
VerbAdmission HoldClaim(World& world, const VerbInvocation& invocation);

[[nodiscard]] VerbBindingToken BindClaimVerbs(VerbDispatcher& dispatcher, World& world);
[[nodiscard]] std::vector<AuthoredQueryBindingToken> BindClaimQueries(AuthoredQueryDispatcher& dispatcher,
                                                                      const World& world);
