// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "NetCore/TfpsClientCore.h"

class FSocket;
class FInternetAddr;

/** NetCore transport implemented with Unreal's cross-platform socket subsystem. */
class FTfpsSocketTransport final : public tfps::ITransport
{
public:
	FTfpsSocketTransport() = default;
	virtual ~FTfpsSocketTransport() override;

	/** Resolves "host:port" and opens a non-blocking UDP socket. */
	bool Open(const FString& HostAndPort, FString& OutError);
	void Close();
	bool IsOpen() const { return Socket != nullptr; }

	virtual bool Send(const uint8_t* Data, size_t Len) override;
	virtual size_t Receive(uint8_t* Buffer, size_t Capacity) override;

private:
	FSocket* Socket = nullptr;
	TSharedPtr<FInternetAddr> ServerAddr;
	TSharedPtr<FInternetAddr> FromAddr;
};
