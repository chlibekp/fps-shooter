// X25519 after TweetNaCl's field arithmetic (16 x 16-bit limbs in int64),
// ChaCha20 and Poly1305 (26-bit limbs) after RFC 8439. Constant-time in the
// secret data.
#include "NetCore/TfpsCrypto.h"

#include <cstring>

namespace tfps::crypto
{
	namespace
	{
		using Gf = int64_t[16];

		void Carry(Gf O)
		{
			for (int I = 0; I < 16; ++I)
			{
				O[I] += (int64_t(1) << 16);
				const int64_t C = O[I] >> 16;
				O[(I + 1) * (I < 15)] += C - 1 + 37 * (C - 1) * (I == 15);
				O[I] -= C << 16;
			}
		}

		void Select(Gf P, Gf Q, int64_t B)
		{
			const int64_t C = ~(B - 1);
			for (int I = 0; I < 16; ++I)
			{
				const int64_t T = C & (P[I] ^ Q[I]);
				P[I] ^= T;
				Q[I] ^= T;
			}
		}

		void Pack(uint8_t* O, const Gf N)
		{
			Gf M, T;
			for (int I = 0; I < 16; ++I) { T[I] = N[I]; }
			Carry(T);
			Carry(T);
			Carry(T);
			for (int J = 0; J < 2; ++J)
			{
				M[0] = T[0] - 0xffed;
				for (int I = 1; I < 15; ++I)
				{
					M[I] = T[I] - 0xffff - ((M[I - 1] >> 16) & 1);
					M[I - 1] &= 0xffff;
				}
				M[15] = T[15] - 0x7fff - ((M[14] >> 16) & 1);
				const int64_t B = (M[15] >> 16) & 1;
				M[14] &= 0xffff;
				Select(T, M, 1 - B);
			}
			for (int I = 0; I < 16; ++I)
			{
				O[2 * I] = static_cast<uint8_t>(T[I] & 0xff);
				O[2 * I + 1] = static_cast<uint8_t>(T[I] >> 8);
			}
		}

		void Unpack(Gf O, const uint8_t* N)
		{
			for (int I = 0; I < 16; ++I) { O[I] = N[2 * I] + (int64_t(N[2 * I + 1]) << 8); }
			O[15] &= 0x7fff;
		}

		void Add(Gf O, const Gf A, const Gf B) { for (int I = 0; I < 16; ++I) { O[I] = A[I] + B[I]; } }
		void Sub(Gf O, const Gf A, const Gf B) { for (int I = 0; I < 16; ++I) { O[I] = A[I] - B[I]; } }

		void Mul(Gf O, const Gf A, const Gf B)
		{
			int64_t T[31] = {};
			for (int I = 0; I < 16; ++I)
			{
				for (int J = 0; J < 16; ++J) { T[I + J] += A[I] * B[J]; }
			}
			for (int I = 0; I < 15; ++I) { T[I] += 38 * T[I + 16]; }
			for (int I = 0; I < 16; ++I) { O[I] = T[I]; }
			Carry(O);
			Carry(O);
		}

		void Square(Gf O, const Gf A) { Mul(O, A, A); }

		void Invert(Gf O, const Gf In)
		{
			Gf C;
			for (int A = 0; A < 16; ++A) { C[A] = In[A]; }
			for (int A = 253; A >= 0; --A)
			{
				Square(C, C);
				if (A != 2 && A != 4) { Mul(C, C, In); }
			}
			for (int A = 0; A < 16; ++A) { O[A] = C[A]; }
		}

		uint32_t Load32(const uint8_t* P) { return uint32_t(P[0]) | (uint32_t(P[1]) << 8) | (uint32_t(P[2]) << 16) | (uint32_t(P[3]) << 24); }
		void Store32(uint8_t* P, uint32_t V)
		{
			P[0] = static_cast<uint8_t>(V);
			P[1] = static_cast<uint8_t>(V >> 8);
			P[2] = static_cast<uint8_t>(V >> 16);
			P[3] = static_cast<uint8_t>(V >> 24);
		}
		uint32_t Rotl(uint32_t V, int N) { return (V << N) | (V >> (32 - N)); }

		void QuarterRound(uint32_t* X, int A, int B, int C, int D)
		{
			X[A] += X[B]; X[D] = Rotl(X[D] ^ X[A], 16);
			X[C] += X[D]; X[B] = Rotl(X[B] ^ X[C], 12);
			X[A] += X[B]; X[D] = Rotl(X[D] ^ X[A], 8);
			X[C] += X[D]; X[B] = Rotl(X[B] ^ X[C], 7);
		}

		void AeadMac(uint8_t Tag[16], const uint8_t* Cipher, size_t Len, const uint8_t* Aad, size_t AadLen, const uint8_t Key[32], const uint8_t Nonce[12])
		{
			uint8_t Block[64];
			ChaCha20Block(Block, Key, 0, Nonce);
			std::vector<uint8_t> Mac;
			Mac.reserve(AadLen + Len + 32);
			Mac.insert(Mac.end(), Aad, Aad + AadLen);
			Mac.resize((Mac.size() + 15) / 16 * 16, 0);
			Mac.insert(Mac.end(), Cipher, Cipher + Len);
			Mac.resize((Mac.size() + 15) / 16 * 16, 0);
			uint8_t Lens[16];
			const uint64_t A = AadLen;
			const uint64_t C = Len;
			for (int I = 0; I < 8; ++I)
			{
				Lens[I] = static_cast<uint8_t>(A >> (8 * I));
				Lens[8 + I] = static_cast<uint8_t>(C >> (8 * I));
			}
			Mac.insert(Mac.end(), Lens, Lens + 16);
			Poly1305(Tag, Mac.data(), Mac.size(), Block);
		}
	}

	void X25519(uint8_t Out[32], const uint8_t Scalar[32], const uint8_t Point[32])
	{
		uint8_t Z[32];
		int64_t X[80];
		Gf A, B, C, D, E, F;
		static const Gf K121665 = {0xDB41, 1};
		for (int I = 0; I < 31; ++I) { Z[I] = Scalar[I]; }
		Z[31] = static_cast<uint8_t>((Scalar[31] & 127) | 64);
		Z[0] &= 248;
		Unpack(X, Point);
		for (int I = 0; I < 16; ++I)
		{
			B[I] = X[I];
			D[I] = A[I] = C[I] = 0;
		}
		A[0] = D[0] = 1;
		for (int I = 254; I >= 0; --I)
		{
			const int64_t R = (Z[I >> 3] >> (I & 7)) & 1;
			Select(A, B, R);
			Select(C, D, R);
			Add(E, A, C);
			Sub(A, A, C);
			Add(C, B, D);
			Sub(B, B, D);
			Square(D, E);
			Square(F, A);
			Mul(A, C, A);
			Mul(C, B, E);
			Add(E, A, C);
			Sub(A, A, C);
			Square(B, A);
			Sub(C, D, F);
			Mul(A, C, K121665);
			Add(A, A, D);
			Mul(C, C, A);
			Mul(A, D, F);
			Mul(D, B, X);
			Square(B, E);
			Select(A, B, R);
			Select(C, D, R);
		}
		for (int I = 0; I < 16; ++I)
		{
			X[I + 16] = A[I];
			X[I + 32] = C[I];
			X[I + 48] = B[I];
			X[I + 64] = D[I];
		}
		Invert(X + 32, X + 32);
		Mul(X + 16, X + 16, X + 32);
		Pack(Out, X + 16);
	}

	void X25519Base(uint8_t Out[32], const uint8_t Scalar[32])
	{
		uint8_t Nine[32] = {9};
		X25519(Out, Scalar, Nine);
	}

	void ChaCha20Block(uint8_t Out[64], const uint8_t Key[32], uint32_t Counter, const uint8_t Nonce[12])
	{
		uint32_t S[16];
		S[0] = 0x61707865; S[1] = 0x3320646e; S[2] = 0x79622d32; S[3] = 0x6b206574;
		for (int I = 0; I < 8; ++I) { S[4 + I] = Load32(Key + 4 * I); }
		S[12] = Counter;
		for (int I = 0; I < 3; ++I) { S[13 + I] = Load32(Nonce + 4 * I); }
		uint32_t X[16];
		std::memcpy(X, S, sizeof(X));
		for (int I = 0; I < 10; ++I)
		{
			QuarterRound(X, 0, 4, 8, 12);
			QuarterRound(X, 1, 5, 9, 13);
			QuarterRound(X, 2, 6, 10, 14);
			QuarterRound(X, 3, 7, 11, 15);
			QuarterRound(X, 0, 5, 10, 15);
			QuarterRound(X, 1, 6, 11, 12);
			QuarterRound(X, 2, 7, 8, 13);
			QuarterRound(X, 3, 4, 9, 14);
		}
		for (int I = 0; I < 16; ++I) { Store32(Out + 4 * I, X[I] + S[I]); }
	}

	void ChaCha20Xor(uint8_t* Data, size_t Len, const uint8_t Key[32], uint32_t Counter, const uint8_t Nonce[12])
	{
		uint8_t Block[64];
		for (size_t Off = 0; Off < Len; Off += 64)
		{
			ChaCha20Block(Block, Key, Counter++, Nonce);
			const size_t N = Len - Off < 64 ? Len - Off : 64;
			for (size_t I = 0; I < N; ++I) { Data[Off + I] ^= Block[I]; }
		}
	}

	void Poly1305(uint8_t Tag[16], const uint8_t* Msg, size_t Len, const uint8_t Key[32])
	{
		const uint32_t R0 = (Load32(Key + 0)) & 0x3ffffff;
		const uint32_t R1 = (Load32(Key + 3) >> 2) & 0x3ffff03;
		const uint32_t R2 = (Load32(Key + 6) >> 4) & 0x3ffc0ff;
		const uint32_t R3 = (Load32(Key + 9) >> 6) & 0x3f03fff;
		const uint32_t R4 = (Load32(Key + 12) >> 8) & 0x00fffff;
		const uint32_t S1 = R1 * 5, S2 = R2 * 5, S3 = R3 * 5, S4 = R4 * 5;
		uint32_t H0 = 0, H1 = 0, H2 = 0, H3 = 0, H4 = 0;
		while (Len > 0)
		{
			uint8_t Buf[17] = {};
			const size_t N = Len < 16 ? Len : 16;
			std::memcpy(Buf, Msg, N);
			uint32_t Hibit = 1u << 24;
			if (N < 16)
			{
				Buf[N] = 1;
				Hibit = 0;
			}
			H0 += (Load32(Buf + 0)) & 0x3ffffff;
			H1 += (Load32(Buf + 3) >> 2) & 0x3ffffff;
			H2 += (Load32(Buf + 6) >> 4) & 0x3ffffff;
			H3 += (Load32(Buf + 9) >> 6) & 0x3ffffff;
			H4 += (Load32(Buf + 12) >> 8) | Hibit;
			uint64_t D0 = uint64_t(H0) * R0 + uint64_t(H1) * S4 + uint64_t(H2) * S3 + uint64_t(H3) * S2 + uint64_t(H4) * S1;
			uint64_t D1 = uint64_t(H0) * R1 + uint64_t(H1) * R0 + uint64_t(H2) * S4 + uint64_t(H3) * S3 + uint64_t(H4) * S2;
			uint64_t D2 = uint64_t(H0) * R2 + uint64_t(H1) * R1 + uint64_t(H2) * R0 + uint64_t(H3) * S4 + uint64_t(H4) * S3;
			uint64_t D3 = uint64_t(H0) * R3 + uint64_t(H1) * R2 + uint64_t(H2) * R1 + uint64_t(H3) * R0 + uint64_t(H4) * S4;
			uint64_t D4 = uint64_t(H0) * R4 + uint64_t(H1) * R3 + uint64_t(H2) * R2 + uint64_t(H3) * R1 + uint64_t(H4) * R0;
			uint32_t C = static_cast<uint32_t>(D0 >> 26); H0 = static_cast<uint32_t>(D0) & 0x3ffffff;
			D1 += C; C = static_cast<uint32_t>(D1 >> 26); H1 = static_cast<uint32_t>(D1) & 0x3ffffff;
			D2 += C; C = static_cast<uint32_t>(D2 >> 26); H2 = static_cast<uint32_t>(D2) & 0x3ffffff;
			D3 += C; C = static_cast<uint32_t>(D3 >> 26); H3 = static_cast<uint32_t>(D3) & 0x3ffffff;
			D4 += C; C = static_cast<uint32_t>(D4 >> 26); H4 = static_cast<uint32_t>(D4) & 0x3ffffff;
			H0 += C * 5; C = H0 >> 26; H0 &= 0x3ffffff;
			H1 += C;
			Msg += N;
			Len -= N;
		}
		uint32_t C = H1 >> 26; H1 &= 0x3ffffff;
		H2 += C; C = H2 >> 26; H2 &= 0x3ffffff;
		H3 += C; C = H3 >> 26; H3 &= 0x3ffffff;
		H4 += C; C = H4 >> 26; H4 &= 0x3ffffff;
		H0 += C * 5; C = H0 >> 26; H0 &= 0x3ffffff;
		H1 += C;
		uint32_t G0 = H0 + 5; C = G0 >> 26; G0 &= 0x3ffffff;
		uint32_t G1 = H1 + C; C = G1 >> 26; G1 &= 0x3ffffff;
		uint32_t G2 = H2 + C; C = G2 >> 26; G2 &= 0x3ffffff;
		uint32_t G3 = H3 + C; C = G3 >> 26; G3 &= 0x3ffffff;
		uint32_t G4 = H4 + C - (1u << 26);
		uint32_t Mask = (G4 >> 31) - 1;
		G0 &= Mask; G1 &= Mask; G2 &= Mask; G3 &= Mask; G4 &= Mask;
		Mask = ~Mask;
		H0 = (H0 & Mask) | G0;
		H1 = (H1 & Mask) | G1;
		H2 = (H2 & Mask) | G2;
		H3 = (H3 & Mask) | G3;
		H4 = (H4 & Mask) | G4;
		H0 = (H0 | (H1 << 26));
		H1 = ((H1 >> 6) | (H2 << 20));
		H2 = ((H2 >> 12) | (H3 << 14));
		H3 = ((H3 >> 18) | (H4 << 8));
		uint64_t F = uint64_t(H0) + Load32(Key + 16); H0 = static_cast<uint32_t>(F);
		F = uint64_t(H1) + Load32(Key + 20) + (F >> 32); H1 = static_cast<uint32_t>(F);
		F = uint64_t(H2) + Load32(Key + 24) + (F >> 32); H2 = static_cast<uint32_t>(F);
		F = uint64_t(H3) + Load32(Key + 28) + (F >> 32); H3 = static_cast<uint32_t>(F);
		Store32(Tag + 0, H0);
		Store32(Tag + 4, H1);
		Store32(Tag + 8, H2);
		Store32(Tag + 12, H3);
	}

	void AeadSeal(uint8_t* Data, size_t Len, const uint8_t* Aad, size_t AadLen, const uint8_t Key[32], const uint8_t Nonce[12], uint8_t Tag[16])
	{
		ChaCha20Xor(Data, Len, Key, 1, Nonce);
		AeadMac(Tag, Data, Len, Aad, AadLen, Key, Nonce);
	}

	bool AeadOpen(uint8_t* Data, size_t Len, const uint8_t* Aad, size_t AadLen, const uint8_t Key[32], const uint8_t Nonce[12], const uint8_t Tag[16])
	{
		uint8_t Expected[16];
		AeadMac(Expected, Data, Len, Aad, AadLen, Key, Nonce);
		uint8_t Diff = 0;
		for (int I = 0; I < 16; ++I) { Diff |= static_cast<uint8_t>(Expected[I] ^ Tag[I]); }
		if (Diff != 0) { return false; }
		ChaCha20Xor(Data, Len, Key, 1, Nonce);
		return true;
	}

	void DeriveSessionKeys(const uint8_t Shared[32], uint8_t ClientToServer[32], uint8_t ServerToClient[32])
	{
		static const uint8_t C2S[12] = {'t', 'f', 'p', 's', '-', 'c', '2', 's', '-', 'k', 'e', 'y'};
		static const uint8_t S2C[12] = {'t', 'f', 'p', 's', '-', 's', '2', 'c', '-', 'k', 'e', 'y'};
		uint8_t Block[64];
		ChaCha20Block(Block, Shared, 0, C2S);
		std::memcpy(ClientToServer, Block, 32);
		ChaCha20Block(Block, Shared, 0, S2C);
		std::memcpy(ServerToClient, Block, 32);
	}
}
