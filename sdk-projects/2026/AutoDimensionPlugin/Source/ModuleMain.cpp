//
//	ModuleMain.cpp
//
//	Main entry point for Vectorworks module code.
//


#include "StdAfx.h"
#include "AutoDimensionObj.h"

#include <fstream>

static void WriteKeeplLoadTrace(const char* message, Sint32 value = -1)
{
	std::ofstream trace("C:\\Users\\keepl\\Downloads\\VectorworksAutoDimensionPlugin\\vw-load-trace-2026.txt", std::ios::app);
	if (trace.is_open()) {
		trace << message;
		if (value != -1) {
			trace << ": " << value;
		}
		trace << "\n";
	}
}

const char * DefaultPluginVWRIdentifier() { return "KeeplAutoDimTest"; }


//------------------------------------------------------------------
// provide SDK version for which this plugin was compiled
extern "C" Sint32 GS_EXTERNAL_ENTRY plugin_module_ver()
{
	WriteKeeplLoadTrace("plugin_module_ver", SDK_VERSION);
	return SDK_VERSION;
}

//------------------------------------------------------------------
// Entry point of the module plug-in for Vectorworks
// More info at: http://developer.vectorworks.net/index.php?title=SDK:Module_Plug-in
//
extern "C" Sint32 GS_EXTERNAL_ENTRY plugin_module_main(Sint32 action, void* moduleInfo, const VWIID& iid, IVWUnknown*& inOutInterface, CallBackPtr cbp)
{
	WriteKeeplLoadTrace("plugin_module_main action", action);

	// initialize VCOM mechanizm
	::GS_InitializeVCOM( cbp );

	Sint32	reply	= 0L;

	using namespace VWFC::PluginSupport;

//	REGISTER_Extension<TesterModule::CExtTool>( GROUPID_ExtensionTool, action, pInfo, ioData, cbp, reply );
//	REGISTER_Extension<TesterModule::CExtMenu>( GROUPID_ExtensionMenu, action, pInfo, ioData, cbp, reply );
//	REGISTER_Extension<TesterModule::CExtObj>( GROUPID_ExtensionParametric, action, pInfo, ioData, cbp, reply );
//	REGISTER_Extension<TesterModule::CExtVSFuncs>( GROUPID_ExtensionVSFunctions, action, pInfo, ioData, cbp, reply );
	WriteKeeplLoadTrace("matches tool iid", iid == AutoDimensionPlugin::CExtAutoDimensionObjDefTool::_GetIID() ? 1 : 0);
	REGISTER_Extension<AutoDimensionPlugin::CExtAutoDimensionObjDefTool>( GROUPID_ExtensionTool, action, moduleInfo, iid, inOutInterface, cbp, reply );
	WriteKeeplLoadTrace("after tool reply", reply);
	WriteKeeplLoadTrace("matches object iid", iid == AutoDimensionPlugin::CExtAutoDimensionObj::_GetIID() ? 1 : 0);
	REGISTER_Extension<AutoDimensionPlugin::CExtAutoDimensionObj>( GROUPID_ExtensionParametric, action, moduleInfo, iid, inOutInterface, cbp, reply );
	WriteKeeplLoadTrace("after object reply", reply);

	return reply;
}
