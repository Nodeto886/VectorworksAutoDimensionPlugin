#include "StdAfx.h"

#include "AutoDimensionObj.h"

using namespace AutoDimensionPlugin;

namespace AutoDimensionPlugin
{
	static const TXString kCategoryUniversalName = "AutoDimensionPlugin";
	static const TXString kCategoryLocalizedName = "Auto Dimension";
	static const TXString kOverallWidth = "AD_OverallWidth";
	static const TXString kOverallHeight = "AD_OverallHeight";
	static const TXString kOverallDepth = "AD_OverallDepth";

	static constexpr double kObjectWidth = 1000.0;
	static constexpr double kObjectHeight = 500.0;
	static constexpr double kObjectDepth = 250.0;

	static SToolDef gToolDef = {
		/*ToolType*/					eExtensionToolType_DefaultPoint,
		/*ParametricName*/				"AutoDimensionObj",
		/*PickAndUpdate*/				ToolDef::pickAndUpdate,
		/*NeedScreenPlane*/				ToolDef::doesntNeedScreenPlane,
		/*Need3DProjection*/			ToolDef::doesntNeed3DView,
		/*Use2DCursor*/					ToolDef::use2DCursor,
		/*ConstrainCursor*/				ToolDef::constrainCursor,
		/*NeedPerspective*/				ToolDef::doesntNeedPerspective,
		/*ShowScreenHints*/				ToolDef::showScreenHints,
		/*NeedsPlanarContext*/			ToolDef::needsPlanarContext,
		/*Message*/						{"AutoDimensionPlugin", "tool_message"},
		/*WaitMoveDistance*/			0,
		/*ConstraintFlags*/				0,
		/*BarDisplay*/					kToolBarDisplay_XYClLaZo,
		/*MinimumCompatibleVersion*/		900,
		/*Title*/						{"AutoDimensionPlugin", "tool_title"},
		/*Category*/					{"AutoDimensionPlugin", "tool_category"},
		/*HelpText*/					{"AutoDimensionPlugin", "tool_help"},
		/*VersionCreated*/				30,
		/*VersoinModified*/				0,
		/*VersoinRetired*/				0,
		/*OverrideHelpID*/				"",
		/*Icon Specifier*/				"",
		/*Cursor Specifier */			""
	};

	static SParametricDef gParametricDef = {
		/*LocalizedName*/				{"AutoDimensionPlugin", "localized_name"},
		/*SubType*/						kParametricSubType_Point,
		/*ResetOnMove*/					false,
		/*ResetOnRotate*/				false,
		/*WallInsertOnEdge*/			false,
		/*WallInsertNoBreak*/			false,
		/*WallInsertHalfBreak*/			false,
		/*WallInsertHideCaps*/			false,
	};

	static SParametricParamDef gArrParameters[] = {
		{ "", {0,0}, "", "", EFieldStyle(0), 0 }
	};

	static void AddSupportedType(std::vector<VectorWorks::Extension::TAutoDimensionTypeInfo>& outvecTypes, const TXString& universalName, const TXString& localizedName, VectorWorks::Extension::EAutoDimensionPlacement placement)
	{
		VectorWorks::Extension::TAutoDimensionTypeInfo typeInfo;
		typeInfo.SetID(universalName, localizedName);
		typeInfo.fvecPlaces.push_back(placement);
		outvecTypes.push_back(typeInfo);
	}
}

// {E18D62E6-8995-4F11-AF9C-960C8F7C2D31}
IMPLEMENT_VWParametricExtension(
	/*Extension class*/	CExtAutoDimensionObj,
	/*Event sink*/		CAutoDimensionObj_EventSink,
	/*Universal name*/	"AutoDimensionObj",
	/*Version*/			1,
	/*UUID*/			0xe18d62e6, 0x8995, 0x4f11, 0xaf, 0x9c, 0x96, 0x0c, 0x8f, 0x7c, 0x2d, 0x31 );

// {062DC681-5F7A-417F-8D72-8E95D14C9C8B}
IMPLEMENT_VWToolExtension(
	/*Extension class*/	CExtAutoDimensionObjDefTool,
	/*Event sink*/		CAutoDimensionObjDefTool_EventSink,
	/*Universal name*/	"AutoDimensionObjTool",
	/*Version*/			1,
	/*UUID*/			0x062dc681, 0x5f7a, 0x417f, 0x8d, 0x72, 0x8e, 0x95, 0xd1, 0x4c, 0x9c, 0x8b );

CExtAutoDimensionObj::CExtAutoDimensionObj(CallBackPtr cbp)
	: VWExtensionParametric(cbp, gParametricDef, gArrParameters)
{
}

CExtAutoDimensionObj::~CExtAutoDimensionObj()
{
}

void CExtAutoDimensionObj::DefineSinks()
{
}

CExtAutoDimensionObjDefTool::CExtAutoDimensionObjDefTool(CallBackPtr cbp)
	: VWExtensionTool(cbp, gToolDef)
{
}

CExtAutoDimensionObjDefTool::~CExtAutoDimensionObjDefTool()
{
}

CAutoDimensionObjDefTool_EventSink::CAutoDimensionObjDefTool_EventSink(IVWUnknown* parent)
	: VWToolDefaultPoint_EventSink(parent, "AutoDimensionObj")
{
}

CAutoDimensionObjDefTool_EventSink::~CAutoDimensionObjDefTool_EventSink()
{
}

CAutoDimensionObj_EventSink::CAutoDimensionObj_EventSink(IVWUnknown* parent)
	: VWParametric_EventSink(parent)
{
}

CAutoDimensionObj_EventSink::~CAutoDimensionObj_EventSink()
{
}

EObjectEvent CAutoDimensionObj_EventSink::OnInitXProperties(CodeRefID objectID)
{
	(void)objectID;
	return kObjectEventNoErr;
}

EObjectEvent CAutoDimensionObj_EventSink::Recalculate()
{
	VWPolygon2DObj rect;
	rect.AddVertex(VWPoint2D(0.0, 0.0));
	rect.AddVertex(VWPoint2D(kObjectWidth, 0.0));
	rect.AddVertex(VWPoint2D(kObjectWidth, kObjectHeight));
	rect.AddVertex(VWPoint2D(0.0, kObjectHeight));
	rect.SetClosed(true);

	return kObjectEventNoErr;
}

bool CAutoDimensionObj_EventSink::OnAutoDimMessage_GetDisplayCategoryName(TXString& outstrCategory_UniversalName, TXString& outstrCategory_LocalizedNameName)
{
	outstrCategory_UniversalName = kCategoryUniversalName;
	outstrCategory_LocalizedNameName = kCategoryLocalizedName;
	return true;
}

bool CAutoDimensionObj_EventSink::OnAutoDimMessage_GetLocalizedTypeName(const TXString& instrDimID_UniversalName, TXString& outstrDimID_LocalizedName)
{
	if (instrDimID_UniversalName == kOverallWidth) {
		outstrDimID_LocalizedName = "Overall Width";
		return true;
	}

	if (instrDimID_UniversalName == kOverallHeight) {
		outstrDimID_LocalizedName = "Overall Height";
		return true;
	}

	if (instrDimID_UniversalName == kOverallDepth) {
		outstrDimID_LocalizedName = "Overall Depth";
		return true;
	}

	return false;
}

bool CAutoDimensionObj_EventSink::OnAutoDimMessage_GetSupportedTypes(EViewTypes inView, std::vector<VectorWorks::Extension::TAutoDimensionTypeInfo>& outvecTypes)
{
	(void)inView;
	outvecTypes.clear();

	AddSupportedType(outvecTypes, kOverallWidth, "Overall Width", VectorWorks::Extension::EAutoDimensionPlacement::eHorizontalBottom);
	AddSupportedType(outvecTypes, kOverallHeight, "Overall Height", VectorWorks::Extension::EAutoDimensionPlacement::eVerticalRight);
	AddSupportedType(outvecTypes, kOverallDepth, "Overall Depth", VectorWorks::Extension::EAutoDimensionPlacement::eHorizontalTop);

	return true;
}

bool CAutoDimensionObj_EventSink::OnAutoDimMessage_GetDimensionDefinitions(EViewTypes inView, const TXString& instrDimID_UniversalName, VectorWorks::Extension::EAutoDimensionPlacement inPlace, std::vector<VectorWorks::Extension::TAutoDimensionDefinition>& outvecDimensions)
{
	(void)inView;
	(void)inPlace;
	outvecDimensions.clear();

	if (instrDimID_UniversalName == kOverallWidth) {
		outvecDimensions.emplace_back(WorldPt3(0.0, 0.0, 0.0), WorldPt3(kObjectWidth, 0.0, 0.0), 0);
		return true;
	}

	if (instrDimID_UniversalName == kOverallHeight) {
		outvecDimensions.emplace_back(WorldPt3(kObjectWidth, 0.0, 0.0), WorldPt3(kObjectWidth, kObjectHeight, 0.0), 0);
		return true;
	}

	if (instrDimID_UniversalName == kOverallDepth) {
		outvecDimensions.emplace_back(WorldPt3(0.0, kObjectHeight, 0.0), WorldPt3(0.0, kObjectHeight, kObjectDepth), 0);
		return true;
	}

	return false;
}
