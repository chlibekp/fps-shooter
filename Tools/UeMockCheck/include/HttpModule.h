#pragma once
// Mock of the UE HTTP module.
#include "Interfaces/IHttpRequest.h"
class FHttpModule
{
public:
	static FHttpModule& Get() { static FHttpModule M; return M; }
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> CreateRequest() { return TSharedRef<IHttpRequest, ESPMode::ThreadSafe>(new IHttpRequest()); }
};
