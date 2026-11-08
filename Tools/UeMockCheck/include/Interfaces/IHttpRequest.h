#pragma once
// Mock of the UE HTTP module request interface (signatures only).
#include "CoreMinimal.h"
class IHttpRequest;
class IHttpResponse;
typedef TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> FHttpRequestPtr;
typedef TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> FHttpResponsePtr;
struct FHttpRequestCompleteDelegate
{
	template <typename UserClass, typename... Payload>
	void BindUObject(UserClass*, void (UserClass::*)(FHttpRequestPtr, FHttpResponsePtr, bool, Payload...), Payload...) {}
};
class IHttpRequest
{
public:
	virtual ~IHttpRequest() = default;
	void SetURL(const FString&) {}
	void SetVerb(const FString&) {}
	void SetHeader(const FString&, const FString&) {}
	void SetContentAsString(const FString&) {}
	FHttpRequestCompleteDelegate& OnProcessRequestComplete() { return Delegate; }
	bool ProcessRequest() { return true; }
	FHttpRequestCompleteDelegate Delegate;
};
