// Copyright TacticalFPS project. Engine-independent NetCore.
#include "NetCore/TfpsJson.h"

#include <cstdlib>
#include <cstring>

namespace tfps
{
	namespace
	{
		class JsonParser
		{
		public:
			explicit JsonParser(const std::string& InText) : Text(InText) {}

			bool Parse(JsonValue& Out, std::string& Err)
			{
				if (!ParseValue(Out, 0)) { Err = Error.empty() ? "json syntax error" : Error; return false; }
				SkipWs();
				if (Pos != Text.size()) { Err = "trailing characters"; return false; }
				return true;
			}

		private:
			const std::string& Text;
			size_t Pos = 0;
			std::string Error;

			void SkipWs()
			{
				while (Pos < Text.size() && (Text[Pos] == ' ' || Text[Pos] == '\n' || Text[Pos] == '\r' || Text[Pos] == '\t')) { ++Pos; }
			}

			bool Fail(const char* Msg) { Error = std::string(Msg) + " at offset " + std::to_string(Pos); return false; }

			bool ParseValue(JsonValue& Out, int Depth)
			{
				if (Depth > 32) { return Fail("nesting too deep"); }
				SkipWs();
				if (Pos >= Text.size()) { return Fail("unexpected end"); }
				const char C = Text[Pos];
				if (C == '{') { return ParseObject(Out, Depth); }
				if (C == '[') { return ParseArray(Out, Depth); }
				if (C == '"') { Out.Kind = JsonValue::EKind::String; return ParseString(Out.String); }
				if (Text.compare(Pos, 4, "true") == 0) { Out.Kind = JsonValue::EKind::Bool; Out.Bool = true; Pos += 4; return true; }
				if (Text.compare(Pos, 5, "false") == 0) { Out.Kind = JsonValue::EKind::Bool; Out.Bool = false; Pos += 5; return true; }
				if (Text.compare(Pos, 4, "null") == 0) { Out.Kind = JsonValue::EKind::Null; Pos += 4; return true; }
				return ParseNumber(Out);
			}

			bool ParseNumber(JsonValue& Out)
			{
				const size_t Start = Pos;
				while (Pos < Text.size() && (std::strchr("+-0123456789.eE", Text[Pos]) != nullptr)) { ++Pos; }
				if (Pos == Start) { return Fail("expected value"); }
				const std::string Num = Text.substr(Start, Pos - Start);
				char* End = nullptr;
				Out.Number = std::strtod(Num.c_str(), &End);
				if (End == nullptr || *End != '\0') { return Fail("bad number"); }
				Out.Kind = JsonValue::EKind::Number;
				return true;
			}

			bool ParseString(std::string& Out)
			{
				++Pos; // opening quote
				Out.clear();
				while (Pos < Text.size())
				{
					const char C = Text[Pos++];
					if (C == '"') { return true; }
					if (C == '\\')
					{
						if (Pos >= Text.size()) { break; }
						const char E = Text[Pos++];
						switch (E)
						{
						case '"': Out += '"'; break;
						case '\\': Out += '\\'; break;
						case '/': Out += '/'; break;
						case 'n': Out += '\n'; break;
						case 't': Out += '\t'; break;
						case 'r': Out += '\r'; break;
						case 'b': Out += '\b'; break;
						case 'f': Out += '\f'; break;
						case 'u':
						{
							if (Pos + 4 > Text.size()) { return Fail("bad escape"); }
							const unsigned long Code = std::strtoul(Text.substr(Pos, 4).c_str(), nullptr, 16);
							Pos += 4;
							// Map names/tags are ASCII; encode BMP code points as UTF-8.
							if (Code < 0x80) { Out += static_cast<char>(Code); }
							else if (Code < 0x800) { Out += static_cast<char>(0xC0 | (Code >> 6)); Out += static_cast<char>(0x80 | (Code & 0x3F)); }
							else { Out += static_cast<char>(0xE0 | (Code >> 12)); Out += static_cast<char>(0x80 | ((Code >> 6) & 0x3F)); Out += static_cast<char>(0x80 | (Code & 0x3F)); }
							break;
						}
						default: return Fail("bad escape");
						}
					}
					else
					{
						Out += C;
					}
				}
				return Fail("unterminated string");
			}

			bool ParseArray(JsonValue& Out, int Depth)
			{
				Out.Kind = JsonValue::EKind::Array;
				++Pos;
				SkipWs();
				if (Pos < Text.size() && Text[Pos] == ']') { ++Pos; return true; }
				while (true)
				{
					Out.Items.emplace_back();
					if (!ParseValue(Out.Items.back(), Depth + 1)) { return false; }
					SkipWs();
					if (Pos >= Text.size()) { return Fail("unterminated array"); }
					if (Text[Pos] == ',') { ++Pos; continue; }
					if (Text[Pos] == ']') { ++Pos; return true; }
					return Fail("expected , or ]");
				}
			}

			bool ParseObject(JsonValue& Out, int Depth)
			{
				Out.Kind = JsonValue::EKind::Object;
				++Pos;
				SkipWs();
				if (Pos < Text.size() && Text[Pos] == '}') { ++Pos; return true; }
				while (true)
				{
					SkipWs();
					if (Pos >= Text.size() || Text[Pos] != '"') { return Fail("expected key"); }
					std::string Key;
					if (!ParseString(Key)) { return false; }
					SkipWs();
					if (Pos >= Text.size() || Text[Pos] != ':') { return Fail("expected :"); }
					++Pos;
					Out.Members.push_back(JsonMember{Key, JsonValue()});
					if (!ParseValue(Out.Members.back().second, Depth + 1)) { return false; }
					SkipWs();
					if (Pos >= Text.size()) { return Fail("unterminated object"); }
					if (Text[Pos] == ',') { ++Pos; continue; }
					if (Text[Pos] == '}') { ++Pos; return true; }
					return Fail("expected , or }");
				}
			}
		};
	}

	bool ParseJson(const std::string& Text, JsonValue& Out, std::string& OutError)
	{
		JsonParser Parser(Text);
		return Parser.Parse(Out, OutError);
	}

	double JsonNumber(const JsonValue& Obj, const char* Key, double Default)
	{
		const JsonValue* V = Obj.Find(Key);
		return (V != nullptr && V->Kind == JsonValue::EKind::Number) ? V->Number : Default;
	}
}
