#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

//=============================================================================
// JsonValue
//
// A lightweight JSON DOM node backed by std::variant. Supports the full
// JSON value set: null, bool, number (double), string, array, object.
//
// Object is stored as std::vector<pair<string, JsonValue>> to preserve
// insertion order. Field lookup is linear â€” fine for config parsing,
// not intended for hot-path use.
//
// This is a load-time / tooling type. Runtime systems should compile
// JSON config into typed structs and numeric lookup tables.
//=============================================================================
class JsonValue
{
public:
	using Array = std::vector<JsonValue>;
	using Object = std::vector<std::pair<std::string, JsonValue>>;

	// -- Construction ---------------------------------------------------------

	JsonValue() : Data(std::monostate{}) {}
	JsonValue(std::nullptr_t) : Data(std::monostate{}) {}
	JsonValue(bool value) : Data(value) {}
	JsonValue(double value) : Data(value) {}
	JsonValue(int value) : Data(static_cast<double>(value)) {}
	JsonValue(std::string value) : Data(std::move(value)) {}
	JsonValue(const char* value) : Data(std::string(value)) {}
	JsonValue(Array value) : Data(std::move(value)) {}
	JsonValue(Object value) : Data(std::move(value)) {}

	// -- Type queries ---------------------------------------------------------

	[[nodiscard]] bool IsNull()   const { return std::holds_alternative<std::monostate>(Data); }
	[[nodiscard]] bool IsBool()   const { return std::holds_alternative<bool>(Data); }
	[[nodiscard]] bool IsNumber() const { return std::holds_alternative<double>(Data); }
	[[nodiscard]] bool IsString() const { return std::holds_alternative<std::string>(Data); }
	[[nodiscard]] bool IsArray()  const { return std::holds_alternative<Array>(Data); }
	[[nodiscard]] bool IsObject() const { return std::holds_alternative<Object>(Data); }

	// -- Value access ---------------------------------------------------------

	[[nodiscard]] bool                AsBool()   const { return std::get<bool>(Data); }
	[[nodiscard]] double              AsNumber() const { return std::get<double>(Data); }
	[[nodiscard]] const std::string&  AsString() const { return std::get<std::string>(Data); }
	[[nodiscard]] const Array&        AsArray()  const { return std::get<Array>(Data); }
	[[nodiscard]] const Object&       AsObject() const { return std::get<Object>(Data); }

	// -- Object field lookup (linear scan) ------------------------------------

	[[nodiscard]] const JsonValue* Find(std::string_view key) const
	{
		if (!IsObject()) return nullptr;
		for (const auto& [k, v] : std::get<Object>(Data))
		{
			if (k == key) return &v;
		}
		return nullptr;
	}

	// -- Size (array or object) -----------------------------------------------

	[[nodiscard]] std::size_t Size() const
	{
		if (IsArray()) return std::get<Array>(Data).size();
		if (IsObject()) return std::get<Object>(Data).size();
		return 0;
	}

private:
	std::variant<std::monostate, bool, double, std::string, Array, Object> Data;
};
