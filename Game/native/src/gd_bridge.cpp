// Minimal Godot binding written directly against the GDExtension C interface
// (gdapi/gdextension_interface.h, dumped from the pinned Godot with
// --dump-gdextension-interface). It registers one class, TfpsNative
// (RefCounted), with one method: request(String) -> String. Requests and
// replies are JSON; tfps_native.cpp implements them. No godot-cpp and no
// code generation are needed to build it.
#include "gdextension_interface.h"
#include "tfps_native.h"

#include <cstring>
#include <new>
#include <string>

namespace
{
GDExtensionClassLibraryPtr Library = nullptr;
GDExtensionInterfaceClassdbRegisterExtensionClass6 RegisterClass = nullptr;
GDExtensionInterfaceClassdbRegisterExtensionClassMethod RegisterMethod = nullptr;
GDExtensionInterfaceClassdbConstructObject2 ConstructObject = nullptr;
GDExtensionInterfaceObjectSetInstance SetInstance = nullptr;
GDExtensionInterfaceStringNameNewWithLatin1Chars NewStringName = nullptr;
GDExtensionInterfaceStringNewWithUtf8CharsAndLen2 NewString = nullptr;
GDExtensionInterfaceStringToUtf8Chars StringToUtf8 = nullptr;
GDExtensionPtrDestructor DestroyString = nullptr;
GDExtensionPtrDestructor DestroyStringName = nullptr;
GDExtensionVariantFromTypeConstructorFunc VariantFromString = nullptr;
GDExtensionTypeFromVariantConstructorFunc StringFromVariant = nullptr;

// Opaque engine values: String and StringName are one pointer wide.
struct alignas(8) GdString
{
	uint8_t Data[8] = {};
};
struct alignas(8) GdStringName
{
	uint8_t Data[8] = {};
};

// Interned names used for the lifetime of the library.
GdStringName ClassName;
GdStringName ParentName;
GdStringName MethodName;
GdStringName ArgName;
GdStringName EmptyName;
GdString EmptyString;

std::string ToStd(const void* GodotString)
{
	const GDExtensionInt Len = StringToUtf8(GodotString, nullptr, 0);
	std::string Out(static_cast<size_t>(Len), '\0');
	if (Len > 0)
	{
		StringToUtf8(GodotString, Out.data(), Len);
	}
	return Out;
}

void FromStd(void* UninitializedString, const std::string& S)
{
	NewString(UninitializedString, S.data(), static_cast<GDExtensionInt>(S.size()));
}

GDExtensionObjectPtr CreateInstance(void* /*ClassUserdata*/, GDExtensionBool /*NotifyPostinitialize*/)
{
	GDExtensionObjectPtr Obj = ConstructObject(&ParentName);
	TfpsNative* Self = new (std::nothrow) TfpsNative();
	SetInstance(Obj, &ClassName, Self);
	return Obj;
}

void FreeInstance(void* /*ClassUserdata*/, GDExtensionClassInstancePtr Instance)
{
	delete static_cast<TfpsNative*>(Instance);
}

void RequestCall(void* /*MethodUserdata*/, GDExtensionClassInstancePtr Instance, const GDExtensionConstVariantPtr* Args,
	GDExtensionInt ArgCount, GDExtensionVariantPtr Return, GDExtensionCallError* Error)
{
	if (ArgCount != 1)
	{
		Error->error = ArgCount < 1 ? GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS : GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS;
		Error->expected = 1;
		return;
	}
	GdString Arg;
	StringFromVariant(&Arg, const_cast<GDExtensionVariantPtr>(Args[0]));
	const std::string Reply = static_cast<TfpsNative*>(Instance)->Request(ToStd(&Arg));
	DestroyString(&Arg);
	GdString Result;
	FromStd(&Result, Reply);
	VariantFromString(Return, &Result);
	DestroyString(&Result);
	Error->error = GDEXTENSION_CALL_OK;
}

void RequestPtrCall(void* /*MethodUserdata*/, GDExtensionClassInstancePtr Instance, const GDExtensionConstTypePtr* Args, GDExtensionTypePtr Return)
{
	const std::string Reply = static_cast<TfpsNative*>(Instance)->Request(ToStd(Args[0]));
	// The return slot holds a valid (empty) String: replace it.
	DestroyString(Return);
	FromStd(Return, Reply);
}

void Initialize(void* /*Userdata*/, GDExtensionInitializationLevel Level)
{
	if (Level != GDEXTENSION_INITIALIZATION_SCENE)
	{
		return;
	}
	NewStringName(&ClassName, "TfpsNative", true);
	NewStringName(&ParentName, "RefCounted", true);
	NewStringName(&MethodName, "request", true);
	NewStringName(&ArgName, "json", true);
	NewStringName(&EmptyName, "", true);
	FromStd(&EmptyString, "");

	GDExtensionClassCreationInfo6 Info;
	std::memset(&Info, 0, sizeof(Info));
	Info.is_exposed = 1;
	Info.is_runtime = 0;
	Info.create_instance_func = &CreateInstance;
	Info.free_instance_func = &FreeInstance;
	RegisterClass(Library, &ClassName, &ParentName, &Info);

	static GDExtensionPropertyInfo ReturnInfo;
	ReturnInfo.type = GDEXTENSION_VARIANT_TYPE_STRING;
	ReturnInfo.name = &EmptyName;
	ReturnInfo.class_name = &EmptyName;
	ReturnInfo.hint = 0;
	ReturnInfo.hint_string = &EmptyString;
	ReturnInfo.usage = 6; // PROPERTY_USAGE_DEFAULT
	static GDExtensionPropertyInfo ArgInfo = ReturnInfo;
	ArgInfo.name = &ArgName;
	static GDExtensionClassMethodArgumentMetadata ArgMeta = GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;

	GDExtensionClassMethodInfo Method;
	std::memset(&Method, 0, sizeof(Method));
	Method.name = &MethodName;
	Method.call_func = &RequestCall;
	Method.ptrcall_func = &RequestPtrCall;
	Method.method_flags = GDEXTENSION_METHOD_FLAGS_DEFAULT;
	Method.has_return_value = 1;
	Method.return_value_info = &ReturnInfo;
	Method.return_value_metadata = GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;
	Method.argument_count = 1;
	Method.arguments_info = &ArgInfo;
	Method.arguments_metadata = &ArgMeta;
	RegisterMethod(Library, &ClassName, &Method);
}

void Deinitialize(void* /*Userdata*/, GDExtensionInitializationLevel Level)
{
	(void)Level;
}
} // namespace

extern "C"
{
#if defined(_WIN32)
	__declspec(dllexport)
#else
	__attribute__((visibility("default")))
#endif
	GDExtensionBool tfps_library_init(GDExtensionInterfaceGetProcAddress GetProc, GDExtensionClassLibraryPtr InLibrary,
		GDExtensionInitialization* Init)
	{
		Library = InLibrary;
		RegisterClass = reinterpret_cast<GDExtensionInterfaceClassdbRegisterExtensionClass6>(GetProc("classdb_register_extension_class6"));
		RegisterMethod = reinterpret_cast<GDExtensionInterfaceClassdbRegisterExtensionClassMethod>(GetProc("classdb_register_extension_class_method"));
		ConstructObject = reinterpret_cast<GDExtensionInterfaceClassdbConstructObject2>(GetProc("classdb_construct_object2"));
		SetInstance = reinterpret_cast<GDExtensionInterfaceObjectSetInstance>(GetProc("object_set_instance"));
		NewStringName = reinterpret_cast<GDExtensionInterfaceStringNameNewWithLatin1Chars>(GetProc("string_name_new_with_latin1_chars"));
		NewString = reinterpret_cast<GDExtensionInterfaceStringNewWithUtf8CharsAndLen2>(GetProc("string_new_with_utf8_chars_and_len2"));
		StringToUtf8 = reinterpret_cast<GDExtensionInterfaceStringToUtf8Chars>(GetProc("string_to_utf8_chars"));
		const auto GetDestructor = reinterpret_cast<GDExtensionInterfaceVariantGetPtrDestructor>(GetProc("variant_get_ptr_destructor"));
		const auto GetFromType = reinterpret_cast<GDExtensionInterfaceGetVariantFromTypeConstructor>(GetProc("get_variant_from_type_constructor"));
		const auto GetToType = reinterpret_cast<GDExtensionInterfaceGetVariantToTypeConstructor>(GetProc("get_variant_to_type_constructor"));
		if (!RegisterClass || !RegisterMethod || !ConstructObject || !SetInstance || !NewStringName || !NewString || !StringToUtf8 || !GetDestructor ||
			!GetFromType || !GetToType)
		{
			return 0; // Godot older than the interface we were built for
		}
		DestroyString = GetDestructor(GDEXTENSION_VARIANT_TYPE_STRING);
		DestroyStringName = GetDestructor(GDEXTENSION_VARIANT_TYPE_STRING_NAME);
		VariantFromString = GetFromType(GDEXTENSION_VARIANT_TYPE_STRING);
		StringFromVariant = GetToType(GDEXTENSION_VARIANT_TYPE_STRING);
		Init->minimum_initialization_level = GDEXTENSION_INITIALIZATION_SCENE;
		Init->userdata = nullptr;
		Init->initialize = &Initialize;
		Init->deinitialize = &Deinitialize;
		return 1;
	}
}
