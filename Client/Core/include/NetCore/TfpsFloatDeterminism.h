// Copyright TacticalFPS project. Engine-independent NetCore.
//
// Include at the top of every .cpp that runs shared simulation math, AFTER
// that file's own header. Forces IEEE-precise float semantics for the rest of
// the translation unit even when the project builds with fast-math options
// (Unreal uses /fp:fast on MSVC), so results match the Rust server bit for bit.
// See Documentation/ARCHITECTURE.md, decision 4.
//
// Pair with TfpsFloatDeterminismEnd.h at the end of the file.

#if defined(_MSC_VER) && !defined(__clang__)
	#pragma float_control(precise, on, push)
	#pragma fp_contract(off)
#elif defined(__clang__)
	#pragma clang fp contract(off)
	#pragma clang fp reassociate(off)
#elif defined(__GNUC__)
	#pragma GCC push_options
	#pragma GCC optimize("fp-contract=off", "no-fast-math")
#endif
