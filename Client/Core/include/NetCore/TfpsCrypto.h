// Packet encryption primitives for the client core: X25519 key agreement
// (RFC 7748) and the ChaCha20-Poly1305 AEAD (RFC 8439). Small portable
// implementations (no dependencies); the Rust side uses the x25519-dalek and
// chacha20poly1305 crates and the golden vectors check both agree.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tfps::crypto
{
	/** Q = n * P on Curve25519 (scalar is clamped as RFC 7748 requires). */
	void X25519(uint8_t Out[32], const uint8_t Scalar[32], const uint8_t Point[32]);
	/** Public key for a secret key (n * 9). */
	void X25519Base(uint8_t Out[32], const uint8_t Scalar[32]);

	/** ChaCha20 block function (RFC 8439 2.3): 64 bytes of keystream. */
	void ChaCha20Block(uint8_t Out[64], const uint8_t Key[32], uint32_t Counter, const uint8_t Nonce[12]);
	/** XORs Data with the ChaCha20 keystream starting at block Counter. */
	void ChaCha20Xor(uint8_t* Data, size_t Len, const uint8_t Key[32], uint32_t Counter, const uint8_t Nonce[12]);
	/** Poly1305 one-time authenticator (RFC 8439 2.5). */
	void Poly1305(uint8_t Tag[16], const uint8_t* Msg, size_t Len, const uint8_t Key[32]);

	/** AEAD_CHACHA20_POLY1305 (RFC 8439 2.8): encrypts Data in place, writes the tag. */
	void AeadSeal(uint8_t* Data, size_t Len, const uint8_t* Aad, size_t AadLen, const uint8_t Key[32], const uint8_t Nonce[12], uint8_t Tag[16]);
	/** Verifies the tag (constant time) and decrypts in place. False if forged. */
	bool AeadOpen(uint8_t* Data, size_t Len, const uint8_t* Aad, size_t AadLen, const uint8_t Key[32], const uint8_t Nonce[12], const uint8_t Tag[16]);

	/**
	 * Session keys from an X25519 shared secret: the first 32 bytes of the
	 * ChaCha20 keystream keyed by the secret, nonce "tfps-c2s-key" or
	 * "tfps-s2c-key" (same derivation as the server).
	 */
	void DeriveSessionKeys(const uint8_t Shared[32], uint8_t ClientToServer[32], uint8_t ServerToClient[32]);
}
