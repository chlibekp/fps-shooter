// Minimal test framework (no dependencies).
#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace tfpstest
{
	struct FTestCase { const char* Name; std::function<void()> Fn; bool bNeedsServer; };
	std::vector<FTestCase>& Registry();
	extern int GFailures;
	struct FRegistrar
	{
		FRegistrar(const char* Name, std::function<void()> Fn, bool bNeedsServer = false) { Registry().push_back({Name, std::move(Fn), bNeedsServer}); }
	};
}

#define TFPS_TEST(Name) \
	static void Name(); \
	static tfpstest::FRegistrar Name##_reg(#Name, Name); \
	static void Name()

#define TFPS_SERVER_TEST(Name) \
	static void Name(); \
	static tfpstest::FRegistrar Name##_reg(#Name, Name, true); \
	static void Name()

#define TFPS_EXPECT(Cond) \
	do { if (!(Cond)) { std::fprintf(stderr, "  FAILED %s:%d: %s\n", __FILE__, __LINE__, #Cond); ++tfpstest::GFailures; } } while (0)

#define TFPS_REQUIRE(Cond) \
	do { if (!(Cond)) { std::fprintf(stderr, "  FAILED %s:%d: %s\n", __FILE__, __LINE__, #Cond); ++tfpstest::GFailures; return; } } while (0)
