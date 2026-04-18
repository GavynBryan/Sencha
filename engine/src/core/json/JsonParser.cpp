#include <core/json/JsonParser.h>
#include <charconv>

namespace
{

class Parser
{
public:
	explicit Parser(std::string_view input) : Input(input), Pos(0) {}

	std::optional<JsonValue> Parse(JsonParseError* error)
	{
		SkipWhitespace();
		auto result = ParseValue();
		if (!result)
		{
			if (error)
			{
				error->Message = Error;
				error->Position = ErrorPos;
			}
			return std::nullopt;
		}
		SkipWhitespace();
		if (Pos < Input.size())
		{
			if (error)
			{
				error->Message = "Unexpected trailing content";
				error->Position = Pos;
			}
			return std::nullopt;
		}
		return result;
	}

private:
	std::string_view Input;
	std::size_t Pos;
	std::string Error;
	std::size_t ErrorPos = 0;

	char Peek() const { return Pos < Input.size() ? Input[Pos] : '\0'; }
	char Advance() { return Pos < Input.size() ? Input[Pos++] : '\0'; }
	bool AtEnd() const { return Pos >= Input.size(); }

	void SetError(std::string msg)
	{
		if (Error.empty())
		{
			Error = std::move(msg);
			ErrorPos = Pos;
		}
	}

	void SkipWhitespace()
	{
		while (Pos < Input.size())
		{
			char c = Input[Pos];
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
				++Pos;
			else
				break;
		}
	}

	std::optional<JsonValue> ParseValue()
	{
		if (AtEnd())
		{
			SetError("Unexpected end of input");
			return std::nullopt;
		}

		char c = Peek();
		switch (c)
		{
		case '"': return ParseString();
		case '{': return ParseObject();
		case '[': return ParseArray();
		case 't': case 'f': return ParseBool();
		case 'n': return ParseNull();
		default:
			if (c == '-' || (c >= '0' && c <= '9'))
				return ParseNumber();
			SetError(std::string("Unexpected character: ") + c);
			return std::nullopt;
		}
	}

	std::optional<JsonValue> ParseString()
	{
		auto str = ParseStringRaw();
		if (!str) return std::nullopt;
		return JsonValue(std::move(*str));
	}

	std::optional<std::string> ParseStringRaw()
	{
		if (Advance() != '"')
		{
			SetError("Expected '\"'");
			return std::nullopt;
		}

		std::string result;
		while (!AtEnd())
		{
			char c = Advance();
			if (c == '"')
				return result;
			if (c == '\\')
			{
				if (AtEnd())
				{
					SetError("Unterminated escape sequence");
					return std::nullopt;
				}
				char e = Advance();
				switch (e)
				{
				case '"':  result += '"'; break;
				case '\\': result += '\\'; break;
				case '/':  result += '/'; break;
				case 'b':  result += '\b'; break;
				case 'f':  result += '\f'; break;
				case 'n':  result += '\n'; break;
				case 'r':  result += '\r'; break;
				case 't':  result += '\t'; break;
				case 'u':
				{
					if (Pos + 4 > Input.size())
					{
						SetError("Incomplete \\u escape");
						return std::nullopt;
					}
					auto hex = Input.substr(Pos, 4);
					Pos += 4;
					unsigned int codepoint = 0;
					auto [ptr, ec] = std::from_chars(hex.data(), hex.data() + 4, codepoint, 16);
					if (ec != std::errc{})
					{
						SetError("Invalid \\u escape");
						return std::nullopt;
					}
					// Encode as UTF-8
					if (codepoint < 0x80)
					{
						result += static_cast<char>(codepoint);
					}
					else if (codepoint < 0x800)
					{
						result += static_cast<char>(0xC0 | (codepoint >> 6));
						result += static_cast<char>(0x80 | (codepoint & 0x3F));
					}
					else
					{
						result += static_cast<char>(0xE0 | (codepoint >> 12));
						result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
						result += static_cast<char>(0x80 | (codepoint & 0x3F));
					}
					break;
				}
				default:
					SetError(std::string("Invalid escape: \\") + e);
					return std::nullopt;
				}
			}
			else
			{
				result += c;
			}
		}

		SetError("Unterminated string");
		return std::nullopt;
	}

	std::optional<JsonValue> ParseNumber()
	{
		std::size_t start = Pos;

		if (Peek() == '-') ++Pos;

		if (!ConsumeDigits())
		{
			SetError("Invalid number");
			return std::nullopt;
		}

		if (Peek() == '.')
		{
			++Pos;
			if (!ConsumeDigits())
			{
				SetError("Invalid number after decimal point");
				return std::nullopt;
			}
		}

		if (Peek() == 'e' || Peek() == 'E')
		{
			++Pos;
			if (Peek() == '+' || Peek() == '-') ++Pos;
			if (!ConsumeDigits())
			{
				SetError("Invalid number exponent");
				return std::nullopt;
			}
		}

		auto numStr = Input.substr(start, Pos - start);
		double value = 0.0;
		auto [ptr, ec] = std::from_chars(numStr.data(), numStr.data() + numStr.size(), value);
		if (ec != std::errc{})
		{
			SetError("Failed to parse number");
			return std::nullopt;
		}

		return JsonValue(value);
	}

	bool ConsumeDigits()
	{
		if (AtEnd() || Peek() < '0' || Peek() > '9')
			return false;
		while (!AtEnd() && Peek() >= '0' && Peek() <= '9')
			++Pos;
		return true;
	}

	std::optional<JsonValue> ParseBool()
	{
		if (Match("true")) return JsonValue(true);
		if (Match("false")) return JsonValue(false);
		SetError("Invalid boolean");
		return std::nullopt;
	}

	std::optional<JsonValue> ParseNull()
	{
		if (Match("null")) return JsonValue(nullptr);
		SetError("Invalid null");
		return std::nullopt;
	}

	std::optional<JsonValue> ParseArray()
	{
		++Pos; // skip '['
		SkipWhitespace();

		JsonValue::Array items;

		if (Peek() == ']')
		{
			++Pos;
			return JsonValue(std::move(items));
		}

		while (true)
		{
			SkipWhitespace();
			auto value = ParseValue();
			if (!value) return std::nullopt;
			items.push_back(std::move(*value));

			SkipWhitespace();
			if (Peek() == ']')
			{
				++Pos;
				return JsonValue(std::move(items));
			}
			if (Peek() != ',')
			{
				SetError("Expected ',' or ']' in array");
				return std::nullopt;
			}
			++Pos;
		}
	}

	std::optional<JsonValue> ParseObject()
	{
		++Pos; // skip '{'
		SkipWhitespace();

		JsonValue::Object entries;

		if (Peek() == '}')
		{
			++Pos;
			return JsonValue(std::move(entries));
		}

		while (true)
		{
			SkipWhitespace();
			if (Peek() != '"')
			{
				SetError("Expected string key in object");
				return std::nullopt;
			}
			auto key = ParseStringRaw();
			if (!key) return std::nullopt;

			SkipWhitespace();
			if (Peek() != ':')
			{
				SetError("Expected ':' after object key");
				return std::nullopt;
			}
			++Pos;

			SkipWhitespace();
			auto value = ParseValue();
			if (!value) return std::nullopt;

			entries.push_back({std::move(*key), std::move(*value)});

			SkipWhitespace();
			if (Peek() == '}')
			{
				++Pos;
				return JsonValue(std::move(entries));
			}
			if (Peek() != ',')
			{
				SetError("Expected ',' or '}' in object");
				return std::nullopt;
			}
			++Pos;
		}
	}

	bool Match(std::string_view literal)
	{
		if (Pos + literal.size() > Input.size())
			return false;
		if (Input.substr(Pos, literal.size()) != literal)
			return false;
		Pos += literal.size();
		return true;
	}
};

} // anonymous namespace

std::optional<JsonValue> JsonParse(std::string_view input, JsonParseError* error)
{
	Parser parser(input);
	return parser.Parse(error);
}
