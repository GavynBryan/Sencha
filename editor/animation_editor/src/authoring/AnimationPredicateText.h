#pragma once

#include <anim/AnimPredicate.h>
#include <anim/AnimRigBinding.h>
#include <anim/AnimSelectSystem.h>
#include <assets/data/DataAssetCache.h>
#include <core/json/JsonValue.h>

#include <string>

// Author-facing form, e.g. "Speed > 2.2 and (Stance == stance.crouch or not Grounded)".
[[nodiscard]] std::string DescribeAnimPredicateTest(const AnimPredicateTest& test);
[[nodiscard]] std::string DescribeAnimPredicateRow(const AnimPredicateRow& row);
[[nodiscard]] std::string DescribeAnimPredicate(const AnimPredicateDecl& predicate);

// Malformed rows read as the parse error.
[[nodiscard]] std::string DescribeAnimPredicate(const JsonValue* rows);

// Reads the row from the selector it was authored in.
[[nodiscard]] std::string DescribeAnimRowSource(const DataAssetCache& data, const AnimRowSource& source);
[[nodiscard]] std::string DescribeAnimRuleRows(const DataAssetCache& data, const std::vector<AnimRowSource>& rows);
// For a failed row, names the row and the value it compared.
[[nodiscard]] std::string DescribeAnimVerdict(const DataAssetCache& data, const AnimBoundRule& rule,
                                              const AnimRuleVerdict& verdict);
