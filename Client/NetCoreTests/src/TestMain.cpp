#include "TestFramework.h"
#include <cstring>

namespace tfpstest
{
	std::vector<FTestCase>& Registry() { static std::vector<FTestCase> R; return R; }
	int GFailures = 0;
}

int main(int Argc, char** Argv)
{
	// Usage: tfps_netcore_tests [--server host:port] [filter]
	const char* Filter = nullptr;
	for (int I = 1; I < Argc; ++I)
	{
		if (std::strcmp(Argv[I], "--server") == 0 && I + 1 < Argc) { ++I; continue; }
		Filter = Argv[I];
	}
	bool bHaveServer = false;
	for (int I = 1; I < Argc; ++I) { if (std::strcmp(Argv[I], "--server") == 0) { bHaveServer = true; } }
	int Run = 0;
	for (const auto& T : tfpstest::Registry())
	{
		if (Filter != nullptr && std::strstr(T.Name, Filter) == nullptr) { continue; }
		if (T.bNeedsServer && !bHaveServer) { std::printf("[ SKIP ] %s (needs --server)\n", T.Name); continue; }
		const int Before = tfpstest::GFailures;
		std::printf("[ RUN  ] %s\n", T.Name);
		std::fflush(stdout);
		T.Fn();
		std::printf("[ %s ] %s\n", tfpstest::GFailures == Before ? " OK " : "FAIL", T.Name);
		++Run;
	}
	std::printf("%d tests run, %d failures\n", Run, tfpstest::GFailures);
	return tfpstest::GFailures == 0 ? 0 : 1;
}
