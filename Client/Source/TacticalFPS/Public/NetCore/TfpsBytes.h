// Copyright TacticalFPS project. Engine-independent NetCore.
// Little-endian byte writer/reader. Mirror of Shared/Protocol/rust/src/bytes.rs.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace tfps
{
	class ByteWriter
	{
	public:
		explicit ByteWriter(std::vector<uint8_t>& InBuf) : Buf(InBuf) {}

		size_t Len() const { return Buf.size(); }
		void U8(uint8_t V) { Buf.push_back(V); }
		void I8(int8_t V) { Buf.push_back(static_cast<uint8_t>(V)); }
		void U16(uint16_t V) { U8(static_cast<uint8_t>(V)); U8(static_cast<uint8_t>(V >> 8)); }
		void I16(int16_t V) { U16(static_cast<uint16_t>(V)); }
		void U32(uint32_t V) { U16(static_cast<uint16_t>(V)); U16(static_cast<uint16_t>(V >> 16)); }
		void I32(int32_t V) { U32(static_cast<uint32_t>(V)); }
		void U64(uint64_t V) { U32(static_cast<uint32_t>(V)); U32(static_cast<uint32_t>(V >> 32)); }
		void F32(float V) { uint32_t B; std::memcpy(&B, &V, 4); U32(B); }
		void Bytes(const uint8_t* Data, size_t N) { Buf.insert(Buf.end(), Data, Data + N); }
		/** u8 length + UTF-8 bytes, truncated to Max bytes on a code point boundary. */
		void Str8(const std::string& S, size_t Max)
		{
			size_t End = S.size() < Max ? S.size() : Max;
			if (End > 255) { End = 255; }
			while (End > 0 && End < S.size() && (static_cast<uint8_t>(S[End]) & 0xC0) == 0x80) { --End; }
			U8(static_cast<uint8_t>(End));
			Bytes(reinterpret_cast<const uint8_t*>(S.data()), End);
		}
		void PadTo(size_t Total) { if (Buf.size() < Total) { Buf.resize(Total, 0); } }
		void PatchU8(size_t At, uint8_t V) { Buf[At] = V; }

	private:
		std::vector<uint8_t>& Buf;
	};

	/** Reader that records failure instead of throwing. Check Ok() after use. */
	class ByteReader
	{
	public:
		ByteReader(const uint8_t* InData, size_t InLen) : Data(InData), Size(InLen) {}

		bool Ok() const { return bOk; }
		void Fail() { bOk = false; }
		size_t Remaining() const { return bOk ? Size - Pos : 0; }
		size_t Position() const { return Pos; }
		const uint8_t* Cursor() const { return Data + Pos; }

		bool Skip(size_t N)
		{
			if (!bOk || Size - Pos < N) { bOk = false; return false; }
			Pos += N;
			return true;
		}
		uint8_t U8()
		{
			if (!bOk || Pos >= Size) { bOk = false; return 0; }
			return Data[Pos++];
		}
		int8_t I8() { return static_cast<int8_t>(U8()); }
		uint16_t U16() { const uint16_t A = U8(); const uint16_t B = U8(); return static_cast<uint16_t>(A | (B << 8)); }
		int16_t I16() { return static_cast<int16_t>(U16()); }
		uint32_t U32() { const uint32_t A = U16(); const uint32_t B = U16(); return A | (B << 16); }
		int32_t I32() { return static_cast<int32_t>(U32()); }
		uint64_t U64() { const uint64_t A = U32(); const uint64_t B = U32(); return A | (B << 32); }
		float F32() { const uint32_t B = U32(); float V; std::memcpy(&V, &B, 4); return V; }
		/** Rejects NaN / infinity (a malicious peer could send them). */
		float F32Finite()
		{
			const uint32_t Bits = U32();
			if ((Bits & 0x7F800000u) == 0x7F800000u) { bOk = false; return 0.0f; } // exponent all ones: inf/NaN
			float V;
			std::memcpy(&V, &Bits, 4);
			return V;
		}
		std::string Str8(size_t Max)
		{
			const size_t N = U8();
			if (N > Max || !bOk || Size - Pos < N) { bOk = false; return std::string(); }
			std::string S(reinterpret_cast<const char*>(Data + Pos), N);
			Pos += N;
			return S;
		}

	private:
		const uint8_t* Data;
		size_t Size;
		size_t Pos = 0;
		bool bOk = true;
	};
}
