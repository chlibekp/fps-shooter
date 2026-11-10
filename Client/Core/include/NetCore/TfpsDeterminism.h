// Copyright TacticalFPS project. Engine-independent NetCore.
#pragma once

#include <cstdint>

namespace tfps
{
	/** Value computed by the Rust implementation (conformance.rs, determinism_selftest_hash). */
	constexpr uint64_t kExpectedSelfTestHash = 0x7f5521bd5eb3e7bbull;

	/** Runs the built-in movement determinism check (~640 simulation steps). */
	uint64_t DeterminismSelfTestHash();

	/**
	 * True if this build's movement simulation is bit-identical to the
	 * server's. If false, client-side prediction will constantly be corrected
	 * (the game still works, but feels worse); the client logs an error.
	 */
	inline bool DeterminismSelfTestPasses() { return DeterminismSelfTestHash() == kExpectedSelfTestHash; }
}
