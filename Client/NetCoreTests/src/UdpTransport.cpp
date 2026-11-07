#include "UdpTransport.h"

#include <chrono>
#include <cstring>
#include <thread>

#if defined(_WIN32)
	#include <winsock2.h>
	#include <ws2tcpip.h>
	using FSockLen = int;
	static const long long kInvalidSock = static_cast<long long>(INVALID_SOCKET);
	static void CloseSock(long long S) { closesocket(static_cast<SOCKET>(S)); }
#else
	#include <arpa/inet.h>
	#include <fcntl.h>
	#include <netdb.h>
	#include <sys/socket.h>
	#include <unistd.h>
	using FSockLen = socklen_t;
	static const long long kInvalidSock = -1;
	static void CloseSock(long long S) { close(static_cast<int>(S)); }
#endif

FUdpTransport::FUdpTransport() : Sock(kInvalidSock)
{
#if defined(_WIN32)
	WSADATA D;
	WSAStartup(MAKEWORD(2, 2), &D);
#endif
}

FUdpTransport::~FUdpTransport()
{
	if (Sock != kInvalidSock) { CloseSock(Sock); }
}

bool FUdpTransport::Open(const std::string& HostPort)
{
	const size_t Colon = HostPort.rfind(':');
	if (Colon == std::string::npos) { return false; }
	const std::string Host = HostPort.substr(0, Colon);
	const std::string Port = HostPort.substr(Colon + 1);
	addrinfo Hints;
	std::memset(&Hints, 0, sizeof(Hints));
	Hints.ai_family = AF_UNSPEC;
	Hints.ai_socktype = SOCK_DGRAM;
	addrinfo* Res = nullptr;
	if (getaddrinfo(Host.c_str(), Port.c_str(), &Hints, &Res) != 0 || Res == nullptr) { return false; }
	const auto S = socket(Res->ai_family, Res->ai_socktype, Res->ai_protocol);
	Sock = static_cast<long long>(S);
	if (Sock == kInvalidSock) { freeaddrinfo(Res); return false; }
	// connect() on UDP: only datagrams from the server are delivered to us.
	const bool bOk = connect(S, Res->ai_addr, static_cast<FSockLen>(Res->ai_addrlen)) == 0;
	freeaddrinfo(Res);
#if defined(_WIN32)
	u_long NonBlocking = 1;
	ioctlsocket(S, FIONBIO, &NonBlocking);
#else
	fcntl(static_cast<int>(S), F_SETFL, fcntl(static_cast<int>(S), F_GETFL, 0) | O_NONBLOCK);
#endif
	return bOk;
}

bool FUdpTransport::Lose()
{
	if (SimulatedLoss <= 0.0f) { return false; }
	Rng = Rng * 1103515245u + 12345u;
	return static_cast<float>((Rng >> 8) & 0xFFFF) / 65536.0f < SimulatedLoss;
}

bool FUdpTransport::Send(const uint8_t* Data, size_t Len)
{
	if (Lose()) { return true; }
	return send(static_cast<decltype(socket(0, 0, 0))>(Sock), reinterpret_cast<const char*>(Data), static_cast<int>(Len), 0) == static_cast<long>(Len);
}

size_t FUdpTransport::Receive(uint8_t* Buffer, size_t Capacity)
{
	while (true)
	{
		const auto N = recv(static_cast<decltype(socket(0, 0, 0))>(Sock), reinterpret_cast<char*>(Buffer), static_cast<int>(Capacity), 0);
		if (N <= 0) { return 0; } // would block, or ICMP error: treat as nothing pending
		if (Lose()) { continue; }
		return static_cast<size_t>(N);
	}
}

double NowSeconds()
{
	using namespace std::chrono;
	return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

void SleepMs(int Ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(Ms));
}
