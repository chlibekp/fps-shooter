// Minimal MOCK of the Unreal Engine API subset used by Client/Source/TacticalFPS.
// Purpose: compile-check the Unreal glue code (syntax, types, our own APIs,
// NetCore usage) on machines without Unreal Engine. It does NOT prove the code
// compiles against the real engine; signatures here mirror UE 5.4 to the best
// of our knowledge. See Tools/UeMockCheck/README.md.
#pragma once
#include <cstdint>
#include <cstdarg>
#include <cwchar>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <cmath>
#include <initializer_list>
#include <functional>
#include <algorithm>

using uint8 = std::uint8_t; using uint16 = std::uint16_t; using uint32 = std::uint32_t; using uint64 = unsigned long long;
using int8 = std::int8_t; using int16 = std::int16_t; using int32 = std::int32_t; using int64 = long long;
using TCHAR = wchar_t;
#define TEXT(x) L##x
#define TACTICALFPS_API
#define UCLASS(...)
#define USTRUCT(...)
#define UENUM(...)
#define UPROPERTY(...)
#define UFUNCTION(...)
#define GENERATED_BODY()
#define PLATFORM_SOCKETSUBSYSTEM 0
#define DECLARE_LOG_CATEGORY_EXTERN(Name, V, C) struct FLogCategory##Name {}; extern FLogCategory##Name Name;
#define DEFINE_LOG_CATEGORY(Name) FLogCategory##Name Name;
#define UE_LOG(Cat, Verb, Fmt, ...) ((void)(Cat), (void)FString::Printf(Fmt, ##__VA_ARGS__))
#define IMPLEMENT_PRIMARY_GAME_MODULE(A, B, C)
#define RETURN_QUICK_DECLARE_CYCLE_STAT(A, B) return TStatId();
struct TStatId {};
enum ELogVerbosity { Log, Warning, Error };
enum EStatGroup { STATGROUP_Tickables };

template <typename T> struct TArray : std::vector<T>
{
	using std::vector<T>::vector;
	int32 Num() const { return static_cast<int32>(this->size()); }
	int32 Add(const T& V) { this->push_back(V); return Num() - 1; }
	void RemoveAt(int32 I) { this->erase(this->begin() + I); }
	template <typename P> int32 RemoveAll(P Pred) { auto It = std::remove_if(this->begin(), this->end(), Pred); int32 N = static_cast<int32>(this->end() - It); this->erase(It, this->end()); return N; }
	void Reset() { this->clear(); }
	bool IsValidIndex(int32 I) const { return I >= 0 && I < Num(); }
};
struct FString
{
	std::wstring S;
	FString() = default;
	FString(const TCHAR* In) : S(In) {}
	const TCHAR* operator*() const { return S.c_str(); }
	FString operator+(const FString& O) const { FString R; R.S = S + O.S; return R; }
	FString operator+(const TCHAR* O) const { FString R; R.S = S + O; return R; }
	FString& operator+=(const TCHAR* O) { S += O; return *this; }
	FString Left(int32 N) const { FString R; R.S = S.substr(0, static_cast<size_t>(N)); return R; }
	bool Split(const FString& D, FString* L, FString* R, int SearchCase, int Dir) const { (void)D; (void)L; (void)R; (void)SearchCase; (void)Dir; return true; }
	template <typename... A> static FString Printf(const TCHAR* Fmt, A... Args) { wchar_t B[512]; std::swprintf(B, 512, Fmt, Args...); return FString(B); }
	const TCHAR* begin() const { return S.c_str(); }
	const TCHAR* end() const { return S.c_str() + S.size(); }
};
namespace ESearchCase { enum Type { IgnoreCase }; }
namespace ESearchDir { enum Type { FromEnd }; }
struct FText { static FText FromString(const FString&) { return FText(); } };
struct FName { FName() = default; FName(const TCHAR*) {} };
static const FName NAME_None; static const FName NAME_DGram;
template <typename T> struct TSharedRef : std::shared_ptr<T> { using std::shared_ptr<T>::shared_ptr; };
template <typename T> struct TSharedPtr : std::shared_ptr<T> { TSharedPtr() = default; TSharedPtr(const TSharedRef<T>& R) : std::shared_ptr<T>(R) {} bool IsValid() const { return this->get() != nullptr; } };
template <typename A, typename B> struct TPair { A Key; B Value; TPair() = default; TPair(const A& InA, const B& InB) : Key(InA), Value(InB) {} };
template <typename K, typename V> struct TMap
{
	std::map<K, V> M;
	V& FindOrAdd(const K& Key) { return M[Key]; }
	V* Find(const K& Key) { auto It = M.find(Key); return It == M.end() ? nullptr : &It->second; }
	const V* Find(const K& Key) const { auto It = M.find(Key); return It == M.end() ? nullptr : &It->second; }
	void Add(const K& Key, const V& Value) { M[Key] = Value; }
	void Remove(const K& Key) { M.erase(Key); }
	void Reset() { M.clear(); }
	int32 Num() const { return static_cast<int32>(M.size()); }
	struct It
	{
		std::map<K, V>* Map; typename std::map<K, V>::iterator I; bool bRemoved = false;
		explicit operator bool() const { return I != Map->end(); }
		void operator++() { if (!bRemoved) { ++I; } bRemoved = false; }
		const K& Key() const { return I->first; }
		V& Value() { return I->second; }
		void RemoveCurrent() { I = Map->erase(I); bRemoved = true; }
	};
	It CreateIterator() { return It{&M, M.begin()}; }
};
template <typename T> struct TSet { std::set<T> S; void Add(const T& V) { S.insert(V); } bool Contains(const T& V) const { return S.count(V) != 0; } };
template <typename T> struct TObjectPtr
{
	T* P = nullptr;
	TObjectPtr() = default; TObjectPtr(T* In) : P(In) {}
	T* operator->() const { return P; } T* Get() const { return P; } operator T*() const { return P; }
	bool operator==(std::nullptr_t) const { return P == nullptr; }
	bool operator!=(std::nullptr_t) const { return P != nullptr; }
};

struct FVector
{
	double X = 0, Y = 0, Z = 0;
	static const FVector ZeroVector;
	FVector() = default; FVector(double A, double B, double C) : X(A), Y(B), Z(C) {} explicit FVector(double A) : X(A), Y(A), Z(A) {}
	FVector operator+(const FVector& O) const { return {X + O.X, Y + O.Y, Z + O.Z}; }
	FVector operator-(const FVector& O) const { return {X - O.X, Y - O.Y, Z - O.Z}; }
	FVector operator*(double S) const { return {X * S, Y * S, Z * S}; }
	FVector operator/(double S) const { return {X / S, Y / S, Z / S}; }
	FVector operator*(const FVector& O) const { return {X * O.X, Y * O.Y, Z * O.Z}; }
	FVector& operator+=(const FVector& O) { X += O.X; Y += O.Y; Z += O.Z; return *this; }
	FVector& operator*=(double S) { X *= S; Y *= S; Z *= S; return *this; }
	FVector& operator-=(const FVector& O) { X -= O.X; Y -= O.Y; Z -= O.Z; return *this; }
	double Size() const { return std::sqrt(X * X + Y * Y + Z * Z); }
	FVector GetSafeNormal() const { double S = Size(); return S > 0 ? *this * (1.0 / S) : FVector(); }
	struct FRotator Rotation() const;
};
inline const FVector FVector::ZeroVector{};
struct FRotator
{
	double Pitch = 0, Yaw = 0, Roll = 0;
	static const FRotator ZeroRotator;
	FRotator() = default; FRotator(double P, double Y, double R) : Pitch(P), Yaw(Y), Roll(R) {}
	static double ClampAxis(double A) { A = std::fmod(A, 360.0); return A < 0 ? A + 360.0 : A; }
	static double NormalizeAxis(double A) { A = ClampAxis(A); return A > 180 ? A - 360 : A; }
};
inline const FRotator FRotator::ZeroRotator{};
inline FRotator FVector::Rotation() const { return FRotator(0, std::atan2(Y, X) * 57.29578, 0); }
struct FRotationMatrix { static FRotationMatrix MakeFromZ(const FVector&) { return FRotationMatrix(); } FRotator Rotator() const { return FRotator(); } };
struct FTransform
{
	static const FTransform Identity;
	FTransform() = default; FTransform(const FRotator&, const FVector&, const FVector&) {}
	FVector GetLocation() const { return FVector(); } FVector GetScale3D() const { return FVector(1.0); }
};
inline const FTransform FTransform::Identity{};
struct FLinearColor { float R, G, B, A; FLinearColor(float r, float g, float b, float a = 1) : R(r), G(g), B(b), A(a) {} static const FLinearColor Gray; };
inline const FLinearColor FLinearColor::Gray{0.5f, 0.5f, 0.5f};
struct FColor { FColor(uint8, uint8, uint8, uint8 = 255) {} };
struct FMath
{
	template <typename T> static T Lerp(const T& A, const T& B, float Alpha) { return A + (B - A) * Alpha; }
	template <typename T> static T Max(T A, T B) { return A > B ? A : B; }
	static float Exp(float V) { return std::exp(V); }
	static float FInterpTo(float C, float T, float Dt, float S) { return C + (T - C) * std::fmin(1.f, Dt * S); }
	static float ClampAngle(float A, float Mn, float Mx) { return A < Mn ? Mn : (A > Mx ? Mx : A); }
	static int32 RandRange(int32 A, int32 B) { return (A + B) / 2; }
	static float FRandRange(float A, float B) { return (A + B) * 0.5f; }
	static float Sqrt(float V) { return std::sqrt(V); }
	template <typename T> static T Clamp(T V, T Lo, T Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
	template <typename T> static T Min(T A, T B) { return A < B ? A : B; }
	static float RadiansToDegrees(float R) { return R * 57.29578f; }
	static float DegreesToRadians(float D) { return D * 0.017453292f; }
	static float Atan2(float Y, float X) { return std::atan2(Y, X); }
	static float Sin(float V) { return std::sin(V); }
	static float Cos(float V) { return std::cos(V); }
};
struct FChar { static bool IsAlnum(TCHAR C) { return std::iswalnum(static_cast<wint_t>(C)) != 0; } };
struct FCString { static int32 Atoi(const TCHAR* S) { return static_cast<int32>(std::wcstol(S, nullptr, 10)); } };
struct FPlatformTime { static double Seconds() { return 0.0; } };
extern uint64 GFrameCounter;
inline uint64 GFrameCounter = 0;
struct FCommandLine { static const TCHAR* Get() { return L""; } };
struct FParse { static bool Value(const TCHAR*, const TCHAR*, FString&) { return false; } };
struct FPaths
{
	static FString ProjectContentDir() { return FString(L"C/"); } static FString ProjectDir() { return FString(L"P/"); }
	template <typename... A> static FString Combine(A... Parts) { FString R; ((R = R + FString(Parts)), ...); return R; }
	static FString ConvertRelativePathToFull(const FString& P) { return P; }
};
struct FFileHelper { static bool LoadFileToString(FString&, const TCHAR*) { return false; } };
struct FUtf8Conv { std::string S; explicit FUtf8Conv(const TCHAR* W) { for (; *W; ++W) S += static_cast<char>(*W); } operator const char*() const { return S.c_str(); } };
struct FTcharConv { std::wstring S; explicit FTcharConv(const char* C) { for (; *C; ++C) S += static_cast<wchar_t>(*C); } operator const TCHAR*() const { return S.c_str(); } };
#define TCHAR_TO_UTF8(X) static_cast<const char*>(FUtf8Conv(X))
#define UTF8_TO_TCHAR(X) static_cast<const TCHAR*>(FTcharConv(X))

// ---- UObject / actors -------------------------------------------------------
class UClass {};
class UWorld;
class UGameInstance;
class UObject
{
public:
	virtual ~UObject() = default;
	bool IsTemplate() const { return false; }
	UWorld* GetWorld() const { return nullptr; }
	template <typename T> T* CreateDefaultSubobject(const TCHAR*) { return new T(); }
};
template <typename T> T* NewObject(UObject* Outer = nullptr) { (void)Outer; return new T(); }
template <typename T> T* LoadObject(UObject* Outer, const TCHAR* Path) { (void)Outer; (void)Path; return nullptr; }
template <typename To, typename From> To* Cast(From* F) { return dynamic_cast<To*>(F); }
template <typename To, typename From> const To* Cast(const From* F) { return dynamic_cast<const To*>(F); }
#define STATIC_CLASS_IMPL static UClass* StaticClass() { static UClass C; return &C; }

enum ETickingGroup { TG_PrePhysics, TG_PostPhysics, TG_PostUpdateWork };
struct FActorTickFunction { bool bCanEverTick = false; ETickingGroup TickGroup = TG_PrePhysics; float TickInterval = 0; };
namespace EComponentMobility { enum Type { Static, Movable }; }
namespace ECollisionEnabled { enum Type { NoCollision, QueryOnly }; }
enum EHorizTextAligment { EHTA_Center };
class UMaterialInterface : public UObject {};
class UMaterialInstanceDynamic : public UMaterialInterface
{
public:
	static UMaterialInstanceDynamic* Create(UMaterialInterface*, UObject*) { return new UMaterialInstanceDynamic(); }
	void SetVectorParameterValue(const TCHAR*, const FLinearColor&) {}
};
class UStaticMesh : public UObject {};
class UActorComponent : public UObject { public: void RegisterComponent() {} void DestroyComponent() {} };
class USceneComponent : public UActorComponent
{
public:
	void SetupAttachment(USceneComponent*) {}
	void SetRelativeLocation(const FVector&) {}
	void SetRelativeRotation(const FRotator&) {}
	void SetRelativeScale3D(const FVector&) {}
	void SetWorldRotation(const FRotator&) {}
	void SetWorldLocation(const FVector&) {}
	void SetWorldLocationAndRotation(const FVector&, const FRotator&) {}
	void SetWorldScale3D(const FVector&) {}
	void SetVisibility(bool) {}
	FVector GetComponentLocation() const { return FVector(); }
	void SetMobility(EComponentMobility::Type) {}
};
class UPrimitiveComponent : public USceneComponent
{
public:
	void SetCollisionEnabled(ECollisionEnabled::Type) {} void SetCastShadow(bool) {} void SetMaterial(int32, UMaterialInterface*) {}
	UMaterialInterface* GetMaterial(int32) const { return nullptr; }
	void SetSimulatePhysics(bool) {} void SetPhysicsLinearVelocity(const FVector&) {} void SetCollisionProfileName(const FName&) {}
};
class UStaticMeshComponent : public UPrimitiveComponent { public: bool SetStaticMesh(UStaticMesh*) { return true; } };
class UInstancedStaticMeshComponent : public UStaticMeshComponent
{
public:
	int32 AddInstance(const FTransform&, bool bWorldSpace = false) { (void)bWorldSpace; return 0; }
	bool GetInstanceTransform(int32, FTransform&, bool bWorldSpace = false) const { (void)bWorldSpace; return true; }
	bool UpdateInstanceTransform(int32, const FTransform&, bool bWorldSpace = false, bool bMarkRenderStateDirty = false, bool bTeleport = false) { (void)bWorldSpace; (void)bMarkRenderStateDirty; (void)bTeleport; return true; }
};
class UCameraComponent : public USceneComponent { public: bool bUsePawnControlRotation = true; void SetFieldOfView(float) {} };
class UTextRenderComponent : public UPrimitiveComponent
{
public:
	void SetHorizontalAlignment(EHorizTextAligment) {} void SetWorldSize(float) {} void SetText(const FText&) {} void SetTextRenderColor(FColor) {}
};
class ULightComponent : public USceneComponent { public: void SetIntensity(float) {} void SetLightColor(const FLinearColor&) {} void SetCastShadows(bool) {} };
class UPointLightComponent : public ULightComponent { public: void SetAttenuationRadius(float) {} };
class UDirectionalLightComponent : public ULightComponent { public: void SetAtmosphereSunLight(bool) {} void SetDynamicShadowDistanceMovableLight(float) {} };
class USkyLightComponent : public USceneComponent { public: uint32 bRealTimeCapture : 1; void SetIntensity(float) {} };
class USkyAtmosphereComponent : public USceneComponent {};
class UExponentialHeightFogComponent : public USceneComponent { public: void SetFogDensity(float) {} };

class AActor : public UObject
{
public:
	FActorTickFunction PrimaryActorTick;
	virtual void BeginPlay() {}
	virtual void Tick(float) {}
	void SetRootComponent(USceneComponent*) {}
	USceneComponent* GetRootComponent() const { return nullptr; }
	UGameInstance* GetGameInstance() const { return nullptr; }
	bool Destroy() { return true; }
	bool SetActorLocation(const FVector&) { return true; }
	FVector GetActorLocation() const { return FVector(); }
	bool SetActorLocationAndRotation(const FVector&, const FRotator&) { return true; }
	STATIC_CLASS_IMPL
};
class AController;
class APawn : public AActor { public: bool bUseControllerRotationYaw = true, bUseControllerRotationPitch = false, bUseControllerRotationRoll = false; STATIC_CLASS_IMPL };
class AController : public AActor { public: void Possess(APawn*) {} APawn* GetPawn() const { return nullptr; } FRotator GetControlRotation() const { return FRotator(); } void SetControlRotation(const FRotator&) {} bool IsLocalController() const { return true; } };
struct FInputModeGameOnly {}; struct FInputModeGameAndUI {};
struct FKey { FKey() = default; FKey(const char*) {} bool IsValid() const { return true; } };
struct EKeys { static const FKey W, S, A, D, SpaceBar, LeftControl, C, LeftShift, LeftAlt, Q, E, RightMouseButton, LeftMouseButton, Z, R, F, F3, Escape, One, Two, Three, Four, MouseScrollUp, MouseScrollDown; };
inline const FKey EKeys::One{"1"}, EKeys::Two{"2"}, EKeys::Three{"3"}, EKeys::Four{"4"}, EKeys::MouseScrollUp{"WU"}, EKeys::MouseScrollDown{"WD"};
inline const FKey EKeys::W{"W"}, EKeys::S{"S"}, EKeys::A{"A"}, EKeys::D{"D"}, EKeys::SpaceBar{"Sp"}, EKeys::LeftControl{"LC"}, EKeys::C{"C"}, EKeys::LeftShift{"LS"}, EKeys::LeftAlt{"LA"}, EKeys::Q{"Q"}, EKeys::E{"E"}, EKeys::RightMouseButton{"RMB"}, EKeys::LeftMouseButton{"LMB"}, EKeys::Z{"Z"}, EKeys::R{"R"}, EKeys::F{"F"}, EKeys::F3{"F3"}, EKeys::Escape{"Esc"};
class APlayerController : public AController
{
public:
	bool bShowMouseCursor = false;
	virtual void PlayerTick(float) {}
	void SetInputMode(const FInputModeGameOnly&) {} void SetInputMode(const FInputModeGameAndUI&) {}
	bool IsInputKeyDown(const FKey) const { return false; }
	bool WasInputKeyJustPressed(const FKey) const { return false; }
	void GetInputMouseDelta(float& X, float& Y) const { X = Y = 0; }
	STATIC_CLASS_IMPL
};
class APlayerCameraManager : public AActor { public: FVector GetCameraLocation() const { return FVector(); } };
class UFont : public UObject {};
class UCanvas : public UObject { public: float ClipX = 1920, ClipY = 1080; };
class AHUD : public AActor
{
public:
	UCanvas* Canvas = nullptr;
	virtual void DrawHUD() {}
	void DrawText(const FString&, FLinearColor, float, float, UFont* = nullptr, float = 1.f, bool = false) {}
	void DrawLine(float, float, float, float, FLinearColor, float = 0.f) {}
	void DrawRect(FLinearColor, float, float, float, float) {}
	APlayerController* GetOwningPlayerController() const { return nullptr; }
	STATIC_CLASS_IMPL
};
class UEngine { public: UFont* GetSmallFont() const { return nullptr; } UFont* GetMediumFont() const { return nullptr; } };
extern UEngine* GEngine;
inline UEngine* GEngine = nullptr;
template <typename T> struct TSubclassOf { TSubclassOf() = default; TSubclassOf(UClass*) {} };
class AGameModeBase : public AActor
{
public:
	TSubclassOf<APawn> DefaultPawnClass; TSubclassOf<APlayerController> PlayerControllerClass; TSubclassOf<AHUD> HUDClass;
	virtual void RestartPlayer(AController*) {}
};
namespace ESpawnActorCollisionHandlingMethod { enum Type { AlwaysSpawn }; }
struct FActorSpawnParameters { ESpawnActorCollisionHandlingMethod::Type SpawnCollisionHandlingOverride; };
class UWorld : public UObject { public: template <typename T> T* SpawnActor(UClass*, const FTransform&, const FActorSpawnParameters&) { return new T(); } };
class FSubsystemCollectionBase {};
class USubsystem : public UObject { public: virtual void Initialize(FSubsystemCollectionBase&) {} virtual void Deinitialize() {} };
class UGameInstanceSubsystem : public USubsystem {};
class UGameInstance : public UObject { public: template <typename T> T* GetSubsystem() const { return nullptr; } };
enum class ETickableTickType { Conditional, Always, Never };
class FTickableGameObject
{
public:
	virtual ~FTickableGameObject() = default;
	virtual void Tick(float DeltaTime) = 0;
	virtual TStatId GetStatId() const = 0;
	virtual bool IsTickable() const { return true; }
	virtual bool IsTickableWhenPaused() const { return false; }
	virtual ETickableTickType GetTickableTickType() const { return ETickableTickType::Conditional; }
};
struct UGameplayStatics { static APlayerCameraManager* GetPlayerCameraManager(const UObject*, int32) { return nullptr; } };

// ---- Sockets ----------------------------------------------------------------
enum ESocketErrors { SE_NO_ERROR, SE_ERROR };
enum class EAddressInfoFlags { Default };
enum class ESocketType { SOCKTYPE_Datagram };
class FInternetAddr
{
public:
	virtual ~FInternetAddr() = default;
	void SetPort(int32) {}
	FName GetProtocolType() const { return FName(); }
	TSharedRef<FInternetAddr> Clone() const { return TSharedRef<FInternetAddr>(new FInternetAddr()); }
	FString ToString(bool) const { return FString(); }
	bool operator==(const FInternetAddr&) const { return true; }
};
struct FAddressInfoResultData { TSharedRef<FInternetAddr> Address; };
struct FAddressInfoResult { ESocketErrors ReturnCode = SE_NO_ERROR; TArray<FAddressInfoResultData> Results; };
class FSocket
{
public:
	bool Close() { return true; }
	bool SetNonBlocking(bool) { return true; }
	bool SetReceiveBufferSize(int32, int32&) { return true; }
	bool SetSendBufferSize(int32, int32&) { return true; }
	bool SendTo(const uint8*, int32, int32&, const FInternetAddr&) { return true; }
	bool RecvFrom(uint8*, int32, int32&, FInternetAddr&) { return false; }
};
class ISocketSubsystem
{
public:
	static ISocketSubsystem* Get(int) { return nullptr; }
	FAddressInfoResult GetAddressInfo(const TCHAR*, const TCHAR*, EAddressInfoFlags, FName, ESocketType) { return {}; }
	TSharedRef<FInternetAddr> CreateInternetAddr(FName) { return TSharedRef<FInternetAddr>(new FInternetAddr()); }
	FSocket* CreateSocket(const FName&, const FString&, const FName&) { return nullptr; }
	void DestroySocket(FSocket*) {}
};
