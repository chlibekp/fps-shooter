// Non-blocking UDP transport for the standalone (non-Unreal) builds.
#pragma once
#include "NetCore/TfpsClientCore.h"
#include <string>

class FUdpTransport final : public tfps::ITransport
{
public:
	FUdpTransport();
	~FUdpTransport() override;
	/** "host:port". Returns false on failure. */
	bool Open(const std::string& HostPort);
	bool Send(const uint8_t* Data, size_t Len) override;
	size_t Receive(uint8_t* Buffer, size_t Capacity) override;
	/** Test hook: drop this fraction of packets in each direction. */
	float SimulatedLoss = 0.0f;

private:
	bool Lose();
	long long Sock;
	unsigned int Rng = 0x1234567u;
};

/** Monotonic seconds. */
double NowSeconds();
void SleepMs(int Ms);
