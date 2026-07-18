#pragma once

namespace AutoDimensionPlugin
{
	using namespace VWFC::PluginSupport;

	class CAutoDimensionObj_EventSink : public VWParametric_EventSink
	{
	public:
						CAutoDimensionObj_EventSink(IVWUnknown* parent);
		virtual			~CAutoDimensionObj_EventSink();

	public:
		virtual EObjectEvent	OnInitXProperties(CodeRefID objectID);
		virtual EObjectEvent	Recalculate();

		virtual bool		OnAutoDimMessage_GetDisplayCategoryName(TXString& outstrCategory_UniversalName, TXString& outstrCategory_LocalizedNameName);
		virtual bool		OnAutoDimMessage_GetLocalizedTypeName(const TXString& instrDimID_UniversalName, TXString& outstrDimID_LocalizedName);
		virtual bool		OnAutoDimMessage_GetSupportedTypes(EViewTypes inView, std::vector<VectorWorks::Extension::TAutoDimensionTypeInfo>& outvecTypes);
		virtual bool		OnAutoDimMessage_GetDimensionDefinitions(EViewTypes inView, const TXString& instrDimID_UniversalName, VectorWorks::Extension::EAutoDimensionPlacement inPlace, std::vector<VectorWorks::Extension::TAutoDimensionDefinition>& outvecDimensions);
	};

	class CExtAutoDimensionObj : public VWExtensionParametric
	{
		DEFINE_VWParametricExtension;
	public:
						CExtAutoDimensionObj(CallBackPtr cbp);
		virtual			~CExtAutoDimensionObj();

	public:
		virtual void	DefineSinks();
	};

	class CExtAutoDimensionObjDefTool : public VWExtensionTool
	{
		DEFINE_VWToolExtension;
	public:
						CExtAutoDimensionObjDefTool(CallBackPtr cbp);
		virtual			~CExtAutoDimensionObjDefTool();
	};

	class CAutoDimensionObjDefTool_EventSink : public VWToolDefaultPoint_EventSink
	{
	public:
						CAutoDimensionObjDefTool_EventSink(IVWUnknown* parent);
		virtual			~CAutoDimensionObjDefTool_EventSink();

		virtual void	HandleComplete();
	};
}
