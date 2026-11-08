#include "tfps_client.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

static void initialize_tfps(ModuleInitializationLevel level)
{
	if (level != MODULE_INITIALIZATION_LEVEL_SCENE)
	{
		return;
	}
	GDREGISTER_CLASS(TfpsClient);
}

static void uninitialize_tfps(ModuleInitializationLevel level)
{
	(void)level;
}

extern "C"
{
	GDExtensionBool GDE_EXPORT tfps_library_init(GDExtensionInterfaceGetProcAddress get_proc_address, const GDExtensionClassLibraryPtr library,
		GDExtensionInitialization* initialization)
	{
		GDExtensionBinding::InitObject init_obj(get_proc_address, library, initialization);
		init_obj.register_initializer(initialize_tfps);
		init_obj.register_terminator(uninitialize_tfps);
		init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
		return init_obj.init();
	}
}
