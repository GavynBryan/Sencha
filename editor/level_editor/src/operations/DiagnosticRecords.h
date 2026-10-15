#pragma once

#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>
#include <zone/ContentRiskRecord.h>

#include <span>
#include <string>

// How an answer spells validation records: severity, stable rule id, the kind
// and hex id of what each is about, and a message. One shape for the world's
// validation and the level's spaces alike.
[[nodiscard]] DataFieldSchema ContentRiskRecordsSchema(std::string key);
[[nodiscard]] JsonValue ContentRiskRecordsJson(std::span<const ContentRiskRecord> records);
