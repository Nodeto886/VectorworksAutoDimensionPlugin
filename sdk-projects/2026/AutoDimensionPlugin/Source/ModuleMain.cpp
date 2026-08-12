//
//	ModuleMain.cpp
//
//	Main entry point for Vectorworks module code.
//


#include "StdAfx.h"
#include "AutoDimensionObj.h"

#if defined(_DEBUG)
#include <cstdlib>
#include <fstream>
#include <string>

static std::string GetKeeplLoadTracePath(const char* fileName)
{
#ifdef _WINDOWS
	const char* tempPath = std::getenv("TEMP");
	if (!tempPath || !*tempPath) {
		tempPath = std::getenv("TMP");
	}
	std::string path = (tempPath && *tempPath) ? tempPath : ".";
	if (!path.empty() && path[path.size() - 1] != '\\' && path[path.size() - 1] != '/') {
		path += '\\';
	}
	path += fileName;
	return path;
#else
	return std::string("/tmp/") + fileName;
#endif
}

static void WriteKeeplLoadTrace(const char* message, Sint32 value = -1)
{
	std::ofstream trace(GetKeeplLoadTracePath("vw-load-trace-2026.txt"), std::ios::app);
	if (trace.is_open()) {
		trace << message;
		if (value != -1) {
			trace << ": " << value;
		}
		trace << "\n";
	}
}
#define VWAD_LOAD_TRACE(...) WriteKeeplLoadTrace(__VA_ARGS__)
#else
#define VWAD_LOAD_TRACE(...) do { } while (false)
#endif

const char * DefaultPluginVWRIdentifier() { return "KeeplAutoDimTest"; }


//------------------------------------------------------------------
// provide SDK version for which this plugin was compiled
extern "C" Sint32 GS_EXTERNAL_ENTRY plugin_module_ver()
{
	VWAD_LOAD_TRACE("plugin_module_ver", SDK_VERSION);
	return SDK_VERSION;
}

//------------------------------------------------------------------
// Entry point of the module plug-in for Vectorworks
// More info at: http://developer.vectorworks.net/index.php?title=SDK:Module_Plug-in
//
extern "C" Sint32 GS_EXTERNAL_ENTRY plugin_module_main(Sint32 action, void* moduleInfo, const VWIID& iid, IVWUnknown*& inOutInterface, CallBackPtr cbp)
{
	VWAD_LOAD_TRACE("plugin_module_main action", action);

	// initialize VCOM mechanizm
	::GS_InitializeVCOM( cbp );

	Sint32	reply	= 0L;

	using namespace VWFC::PluginSupport;

//	REGISTER_Extension<TesterModule::CExtTool>( GROUPID_ExtensionTool, action, pInfo, ioData, cbp, reply );
//	REGISTER_Extension<TesterModule::CExtMenu>( GROUPID_ExtensionMenu, action, pInfo, ioData, cbp, reply );
//	REGISTER_Extension<TesterModule::CExtObj>( GROUPID_ExtensionParametric, action, pInfo, ioData, cbp, reply );
//	REGISTER_Extension<TesterModule::CExtVSFuncs>( GROUPID_ExtensionVSFunctions, action, pInfo, ioData, cbp, reply );
	VWAD_LOAD_TRACE("matches tool iid", iid == AutoDimensionPlugin::CExtAutoDimensionObjDefTool::_GetIID() ? 1 : 0);
	REGISTER_Extension<AutoDimensionPlugin::CExtAutoDimensionObjDefTool>( GROUPID_ExtensionTool, action, moduleInfo, iid, inOutInterface, cbp, reply );
	VWAD_LOAD_TRACE("after tool reply", reply);
	VWAD_LOAD_TRACE("matches object iid", iid == AutoDimensionPlugin::CExtAutoDimensionObj::_GetIID() ? 1 : 0);
	REGISTER_Extension<AutoDimensionPlugin::CExtAutoDimensionObj>( GROUPID_ExtensionParametric, action, moduleInfo, iid, inOutInterface, cbp, reply );
	VWAD_LOAD_TRACE("after object reply", reply);

	return reply;
}
