// Copyright TacticalFPS project. Engine-independent NetCore.
// Minimal JSON reader (objects, arrays, strings, numbers, bools, null) used
// for shared definition files (maps, weapons) so the standalone build has no
// dependencies and parses numbers exactly like the Rust side (f64 -> f32).
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace tfps
{
	struct JsonMember;

	struct JsonValue
	{
		enum class EKind { Null, Bool, Number, String, Array, Object } Kind = EKind::Null;
		double Number = 0.0;
		bool Bool = false;
		std::string String;
		std::vector<JsonValue> Items;
		/** Object members in document order (a vector of an incomplete type is valid C++17;
		 *  std::pair<std::string, JsonValue> here would not be). */
		std::vector<JsonMember> Members;

		const JsonValue* Find(const char* Key) const;
	};

	struct JsonMember
	{
		std::string first;
		JsonValue second;
	};

	inline const JsonValue* JsonValue::Find(const char* Key) const
	{
		for (const JsonMember& M : Members)
		{
			if (M.first == Key) { return &M.second; }
		}
		return nullptr;
	}

	/** Parses a complete JSON document. Returns false and sets OutError on failure. */
	bool ParseJson(const std::string& Text, JsonValue& Out, std::string& OutError);

	/** Number member or default. */
	double JsonNumber(const JsonValue& Obj, const char* Key, double Default);
}
