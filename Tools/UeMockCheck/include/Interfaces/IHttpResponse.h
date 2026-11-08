#pragma once
// Mock of the UE HTTP module response interface.
#include "Interfaces/IHttpRequest.h"
class IHttpResponse
{
public:
	virtual ~IHttpResponse() = default;
	FString GetContentAsString() const { return FString(); }
	int32 GetResponseCode() const { return 200; }
};
