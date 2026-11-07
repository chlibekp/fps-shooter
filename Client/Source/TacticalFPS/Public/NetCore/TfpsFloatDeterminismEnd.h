// Copyright TacticalFPS project. Engine-independent NetCore.
// Closes the region opened by TfpsFloatDeterminism.h.
#if defined(_MSC_VER) && !defined(__clang__)
	#pragma float_control(pop)
#elif defined(__clang__)
	// clang fp pragmas are scoped to the end of the translation unit.
#elif defined(__GNUC__)
	#pragma GCC pop_options
#endif
