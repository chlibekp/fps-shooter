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
	struct JsonValue
	{
		enum class EKind { Null, Bool, Number, String, Array, Object } Kind = EKind::Null;
		double Number = 0.0;
		bool Bool = false;
		std::string String;
		std::vector<JsonValue> Items;
		std::vector<std::pair<std::string, JsonValue>> Members;

		const JsonValue* Find(const char* Key) const
		{
			for (const auto& M : Members)
			{
				if (M.first == Key) { return &M.second; }
			}
			return nullptr;
		}
	};

	/** Parses a complete JSON document. Returns false and sets OutError on failure. */
	bool ParseJson(const std::string& Text, JsonValue& Out, std::string& OutError);

	/** Number member or default. */
	double JsonNumber(const JsonValue& Obj, const char* Key, double Default);
}
