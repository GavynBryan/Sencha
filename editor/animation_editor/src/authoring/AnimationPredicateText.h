#pragma once

#include <anim/AnimPredicate.h>
#include <anim/AnimRigBinding.h>
#include <anim/AnimSelectSystem.h>
#include <assets/data/DataAssetCache.h>
#include <core/json/JsonValue.h>

#include <string>

// Reads a predicate the way an author wrote it: "Speed > 2.2 and (Stance ==
// stance.crouch or not Grounded)". Every rule table, row and verdict in the
// animation editor shows predicates through this, so one condition always
// reads the same.
[[nodiscard]] std::string DescribeAnimPredicateTest(const AnimPredicateTest& test);
[[nodiscard]] std::string DescribeAnimPredicateRow(const AnimPredicateRow& row);
[[nodiscard]] std::string DescribeAnimPredicate(const AnimPredicateDecl& predicate);

// The same, from the authored rows. Malformed rows read as the parse error.
[[nodiscard]] std::string DescribeAnimPredicate(const JsonValue* rows);

// One row of a flattened rule, read from the selector it was authored in.
[[nodiscard]] std::string DescribeAnimRowSource(const DataAssetCache& data, const AnimRowSource& source);
// A flattened rule's enter or stay, row by row from their sources.
[[nodiscard]] std::string DescribeAnimRuleRows(const DataAssetCache& data, const std::vector<AnimRowSource>& rows);
// Why a rule did not win, in words: its verdict and, for a failed row, the row
// and what it compared.
[[nodiscard]] std::string DescribeAnimVerdict(const DataAssetCache& data, const AnimBoundRule& rule,
                                              const AnimRuleVerdict& verdict);
