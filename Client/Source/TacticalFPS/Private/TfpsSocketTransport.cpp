// Copyright TacticalFPS project.
#include "TfpsSocketTransport.h"
#include "TacticalFPS.h"

#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

FTfpsSocketTransport::~FTfpsSocketTransport()
{
	Close();
}

bool FTfpsSocketTransport::Open(const FString& HostAndPort, FString& OutError)
{
	Close();
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		OutError = TEXT("no socket subsystem");
		return false;
	}

	FString Host;
	FString PortString;
	if (!HostAndPort.Split(TEXT(":"), &Host, &PortString, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
	{
		OutError = FString::Printf(TEXT("expected host:port, got '%s'"), *HostAndPort);
		return false;
	}
	const int32 Port = FCString::Atoi(*PortString);
	if (Port <= 0 || Port > 65535)
	{
		OutError = TEXT("invalid port");
		return false;
	}

	const FAddressInfoResult Resolved = SocketSubsystem->GetAddressInfo(*Host, nullptr, EAddressInfoFlags::Default, NAME_None, ESocketType::SOCKTYPE_Datagram);
	if (Resolved.ReturnCode != SE_NO_ERROR || Resolved.Results.Num() == 0)
	{
		OutError = FString::Printf(TEXT("cannot resolve '%s'"), *Host);
		return false;
	}
	ServerAddr = Resolved.Results[0].Address->Clone();
	ServerAddr->SetPort(Port);
	FromAddr = SocketSubsystem->CreateInternetAddr(ServerAddr->GetProtocolType());

	Socket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("TfpsGameClient"), ServerAddr->GetProtocolType());
	if (Socket == nullptr)
	{
		OutError = TEXT("cannot create UDP socket");
		return false;
	}
	Socket->SetNonBlocking(true);
	int32 ActualSize = 0;
	Socket->SetReceiveBufferSize(1 << 20, ActualSize);
	Socket->SetSendBufferSize(1 << 18, ActualSize);
	UE_LOG(LogTfps, Log, TEXT("UDP socket open, server %s"), *ServerAddr->ToString(true));
	return true;
}

void FTfpsSocketTransport::Close()
{
	if (Socket != nullptr)
	{
		Socket->Close();
		if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			SocketSubsystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}
}

bool FTfpsSocketTransport::Send(const uint8_t* Data, size_t Len)
{
	if (Socket == nullptr || !ServerAddr.IsValid())
	{
		return false;
	}
	int32 BytesSent = 0;
	return Socket->SendTo(Data, static_cast<int32>(Len), BytesSent, *ServerAddr) && BytesSent == static_cast<int32>(Len);
}

size_t FTfpsSocketTransport::Receive(uint8_t* Buffer, size_t Capacity)
{
	if (Socket == nullptr)
	{
		return 0;
	}
	while (true)
	{
		int32 BytesRead = 0;
		if (!Socket->RecvFrom(Buffer, static_cast<int32>(Capacity), BytesRead, *FromAddr) || BytesRead <= 0)
		{
			return 0;
		}
		// Only the server may talk to us.
		if (*FromAddr == *ServerAddr)
		{
			return static_cast<size_t>(BytesRead);
		}
	}
}
