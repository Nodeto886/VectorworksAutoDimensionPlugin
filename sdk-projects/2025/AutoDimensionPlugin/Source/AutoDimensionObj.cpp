#include "StdAfx.h"

#include "AutoDimensionObj.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <sstream>

using namespace AutoDimensionPlugin;

namespace AutoDimensionPlugin
{
	static const TXString kCategoryUniversalName = "KeeplAutoDimTestCategory";
	static const TXString kCategoryLocalizedName = "Keepl AutoDim Test";
	static const TXString kOverallWidth = "AD_OverallWidth";
	static const TXString kOverallHeight = "AD_OverallHeight";
	static const TXString kOverallDepth = "AD_OverallDepth";
	static const TXString kSourceUUIDParam = "SourceUUID";
	static const char* kRuntimeTracePath = "C:\\Users\\keepl\\Downloads\\VectorworksAutoDimensionPlugin\\vw-autodim-runtime-2025.txt";

	static void WriteRuntimeTrace(const std::string& message)
	{
		std::ofstream trace(kRuntimeTracePath, std::ios::app);
		if (trace.is_open()) {
			trace << message << '\n';
		}
	}

	static std::string FormatPoint(const WorldPt3& point)
	{
		std::ostringstream output;
		output.precision(12);
		output << '(' << point.x << ',' << point.y << ',' << point.z << ')';
		return output.str();
	}

	static std::string DescribeObject(MCObjectHandle object)
	{
		if (!object) {
			return "handle=null";
		}

		WorldCube bounds;
		gSDK->GetObjectCube(object, bounds);
		UuidStorage uuid;
		gSDK->GetObjectUuidN(object, uuid);

		std::ostringstream output;
		output.precision(12);
		output << "handle=" << reinterpret_cast<const void*>(object)
			<< " type=" << gSDK->GetObjectTypeN(object)
			<< " uuid=" << uuid.ToString().operator const char*()
			<< " cube=[" << bounds.MinX() << ',' << bounds.MinY() << ',' << bounds.MinZ()
			<< " -> " << bounds.MaxX() << ',' << bounds.MaxY() << ',' << bounds.MaxZ() << ']';
		return output.str();
	}

	static const char* GetDimensionTraceName(const TXString& dimensionID)
	{
		if (dimensionID == kOverallWidth) return "OverallWidth";
		if (dimensionID == kOverallHeight) return "OverallHeight";
		if (dimensionID == kOverallDepth) return "OverallDepth";
		return "Unknown";
	}

	static bool IsSupportedSource(MCObjectHandle object)
	{
		return object && !VWParametricObj::IsParametricObject(object, "KeeplAutoDimTestObj");
	}

	static bool LinkProxyToSource(MCObjectHandle proxyObject, MCObjectHandle sourceObject)
	{
		UuidStorage sourceUUID;
		if (!proxyObject || !sourceObject || !gSDK->GetObjectUuidN(sourceObject, sourceUUID) || sourceUUID.IsEmpty()) {
			WriteRuntimeTrace("link-proxy source UUID failed");
			return false;
		}

		VWParametricObj parametric(proxyObject);
		parametric.SetParamString(kSourceUUIDParam, sourceUUID.ToString());
		WriteRuntimeTrace(std::string("link-proxy source_uuid=") + sourceUUID.ToString().operator const char*());
		gSDK->ResetObject(proxyObject);
		return true;
	}

	static MCObjectHandle CreateLinkedProxy(MCObjectHandle sourceObject)
	{
		if (!IsSupportedSource(sourceObject)) {
			return nullptr;
		}

		WorldCube bounds;
		gSDK->GetObjectCube(sourceObject, bounds);
		VWParametricObj proxy("KeeplAutoDimTestObj", VWPoint2D(bounds.MinX(), bounds.MinY()));
		MCObjectHandle proxyObject = proxy;
		WriteRuntimeTrace("create-linked-proxy source " + DescribeObject(sourceObject));
		if (!LinkProxyToSource(proxyObject, sourceObject)) {
			if (proxyObject) {
				gSDK->DeleteObject(proxyObject);
			}
			return nullptr;
		}

		WriteRuntimeTrace("create-linked-proxy result " + DescribeObject(proxyObject));
		return proxyObject;
	}

	struct SSourceBounds
	{
		double minX = std::numeric_limits<double>::max();
		double minY = std::numeric_limits<double>::max();
		double minZ = std::numeric_limits<double>::max();
		double maxX = std::numeric_limits<double>::lowest();
		double maxY = std::numeric_limits<double>::lowest();
		double maxZ = std::numeric_limits<double>::lowest();

		void Add(const VWPoint3D& point)
		{
			minX = std::min(minX, point.x);
			minY = std::min(minY, point.y);
			minZ = std::min(minZ, point.z);
			maxX = std::max(maxX, point.x);
			maxY = std::max(maxY, point.y);
			maxZ = std::max(maxZ, point.z);
		}

		bool IsValid() const
		{
			return minX <= maxX && minY <= maxY && minZ <= maxZ;
		}
	};

	static MCObjectHandle GetDefinitionObject(MCObjectHandle object)
	{
		MCObjectHandle proxyParent = gSDK->GetProxyParent(object);
		return proxyParent ? proxyParent : object;
	}

	static MCObjectHandle GetSourceObject(MCObjectHandle object)
	{
		VWParametricObj parametric(GetDefinitionObject(object));
		const TXString sourceUUID = parametric.GetParamString(kSourceUUIDParam);
		if (sourceUUID.GetLength() == 0) {
			return nullptr;
		}

		return gSDK->GetObjectByUuidN(UuidStorage(sourceUUID));
	}

	static bool GetSourceBounds(MCObjectHandle object, SSourceBounds& outBounds)
	{
		MCObjectHandle definitionObject = GetDefinitionObject(object);
		MCObjectHandle sourceObject = GetSourceObject(definitionObject);
		if (!sourceObject || sourceObject == definitionObject) {
			return false;
		}

		WorldCube worldBounds;
		gSDK->GetObjectCube(sourceObject, worldBounds);

		VWParametricObj parametric(definitionObject);
		VWTransformMatrix objectToWorld;
		parametric.GetObjectToWorldTransform(objectToWorld);

		const std::array<double, 2> xs = { worldBounds.MinX(), worldBounds.MaxX() };
		const std::array<double, 2> ys = { worldBounds.MinY(), worldBounds.MaxY() };
		const std::array<double, 2> zs = { worldBounds.MinZ(), worldBounds.MaxZ() };
		for (double x : xs) {
			for (double y : ys) {
				for (double z : zs) {
					outBounds.Add(objectToWorld.InversePointTransform(VWPoint3D(x, y, z)));
				}
			}
		}

		return outBounds.IsValid();
	}

	static bool IsTopView(EViewTypes view)
	{
		return view == EViewTypes::Top || view == EViewTypes::Bottom ||
			view == EViewTypes::TopBottomCut || view == EViewTypes::TopPlan ||
			view == EViewTypes::NotSet;
	}

	static bool IsSideView(EViewTypes view)
	{
		return view == EViewTypes::Left || view == EViewTypes::Right || view == EViewTypes::LeftRightCut;
	}

	static void GetViewLengths(EViewTypes view, const SSourceBounds& bounds, double& outWidth, double& outHeight, double& outDepth)
	{
		const double sizeX = bounds.maxX - bounds.minX;
		const double sizeY = bounds.maxY - bounds.minY;
		const double sizeZ = bounds.maxZ - bounds.minZ;

		if (IsTopView(view)) {
			outWidth = sizeX;
			outHeight = sizeY;
			outDepth = sizeZ;
		}
		else if (IsSideView(view)) {
			outWidth = sizeY;
			outHeight = sizeZ;
			outDepth = sizeX;
		}
		else {
			outWidth = sizeX;
			outHeight = sizeZ;
			outDepth = sizeY;
		}
	}

	static bool GetDimensionPoints(EViewTypes view, const TXString& dimensionID, const SSourceBounds& bounds, WorldPt3& outStart, WorldPt3& outEnd)
	{
		if (dimensionID == kOverallWidth) {
			if (IsSideView(view)) {
				outStart = WorldPt3(bounds.minX, bounds.minY, bounds.minZ);
				outEnd = WorldPt3(bounds.minX, bounds.maxY, bounds.minZ);
			}
			else {
				outStart = WorldPt3(bounds.minX, bounds.minY, bounds.minZ);
				outEnd = WorldPt3(bounds.maxX, bounds.minY, bounds.minZ);
			}
			return true;
		}

		if (dimensionID == kOverallHeight) {
			if (IsTopView(view)) {
				outStart = WorldPt3(bounds.maxX, bounds.minY, bounds.minZ);
				outEnd = WorldPt3(bounds.maxX, bounds.maxY, bounds.minZ);
			}
			else {
				outStart = WorldPt3(bounds.maxX, bounds.minY, bounds.minZ);
				outEnd = WorldPt3(bounds.maxX, bounds.minY, bounds.maxZ);
			}
			return true;
		}

		if (dimensionID == kOverallDepth) {
			if (IsTopView(view)) {
				outStart = WorldPt3(bounds.minX, bounds.maxY, bounds.minZ);
				outEnd = WorldPt3(bounds.minX, bounds.maxY, bounds.maxZ);
			}
			else if (IsSideView(view)) {
				outStart = WorldPt3(bounds.minX, bounds.maxY, bounds.maxZ);
				outEnd = WorldPt3(bounds.maxX, bounds.maxY, bounds.maxZ);
			}
			else {
				outStart = WorldPt3(bounds.minX, bounds.minY, bounds.maxZ);
				outEnd = WorldPt3(bounds.minX, bounds.maxY, bounds.maxZ);
			}
			return true;
		}

		return false;
	}

	static SToolDef gToolDef = {
		/*ToolType*/					eExtensionToolType_Normal,
		/*ParametricName*/				"",
		/*PickAndUpdate*/				ToolDef::pickAndUpdate,
		/*NeedScreenPlane*/				ToolDef::doesntNeedScreenPlane,
		/*Need3DProjection*/			ToolDef::doesntNeed3DView,
		/*Use2DCursor*/					ToolDef::use2DCursor,
		/*ConstrainCursor*/				ToolDef::constrainCursor,
		/*NeedPerspective*/				ToolDef::doesntNeedPerspective,
		/*ShowScreenHints*/				ToolDef::showScreenHints,
		/*NeedsPlanarContext*/			ToolDef::needsPlanarContext,
		/*Message*/						{"KeeplAutoDim", "tool_message"},
		/*WaitMoveDistance*/			0,
		/*ConstraintFlags*/				0,
		/*BarDisplay*/					kToolBarDisplay_XYClLaZo,
		/*MinimumCompatibleVersion*/		900,
		/*Title*/						{"KeeplAutoDim", "tool_title"},
		/*Category*/					{"KeeplAutoDim", "tool_category"},
		/*HelpText*/					{"KeeplAutoDim", "tool_help"},
		/*VersionCreated*/				30,
		/*VersoinModified*/				0,
		/*VersoinRetired*/				0,
		/*OverrideHelpID*/				"",
		/*Icon Specifier*/				"KeeplAutoDimTest/Images/KeeplAutoDimTestObjTool.png",
		/*Cursor Specifier */			""
	};

	static SParametricDef gParametricDef = {
		/*LocalizedName*/				{"KeeplAutoDim", "localized_name"},
		/*SubType*/						kParametricSubType_Point,
		/*ResetOnMove*/					false,
		/*ResetOnRotate*/				false,
		/*WallInsertOnEdge*/			false,
		/*WallInsertNoBreak*/			false,
		/*WallInsertHalfBreak*/			false,
		/*WallInsertHideCaps*/			false,
	};

	static SParametricParamDef gArrParameters[] = {
		{ "SourceUUID", {"KeeplAutoDim", "source_uuid"}, "", "", kFieldText, 0 },
		{ "Enabled", {"KeeplAutoDim", "param1"}, "True", "True", kFieldBoolean, 0 },
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
	/*Universal name*/	"KeeplAutoDimTestObj",
	/*Version*/			1,
	/*UUID*/			0xe18d62e6, 0x8995, 0x4f11, 0xaf, 0x9c, 0x96, 0x0c, 0x8f, 0x7c, 0x2d, 0x31 );

// {062DC681-5F7A-417F-8D72-8E95D14C9C8B}
IMPLEMENT_VWToolExtension(
	/*Extension class*/	CExtAutoDimensionObjDefTool,
	/*Event sink*/		CAutoDimensionObjDefTool_EventSink,
	/*Universal name*/	"KeeplAutoDimSelectionTool",
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
	: VWTool_EventSink(parent)
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
	MCObjectHandle definitionObject = GetDefinitionObject(fhObject);
	MCObjectHandle sourceObject = GetSourceObject(definitionObject);
	WriteRuntimeTrace("recalculate proxy " + DescribeObject(fhObject));
	if (!sourceObject || sourceObject == definitionObject) {
		WriteRuntimeTrace("recalculate source resolution failed");
		return kObjectEventNoErr;
	}
	WriteRuntimeTrace("recalculate source " + DescribeObject(sourceObject));

	MCObjectHandle duplicate = gSDK->DuplicateObject(sourceObject);
	if (duplicate) {
		WriteRuntimeTrace("recalculate duplicate created " + DescribeObject(duplicate));
		VWParametricObj parametric(definitionObject);
		VWTransformMatrix objectToWorld;
		parametric.GetObjectToWorldTransform(objectToWorld);
		gSDK->TransformObject(duplicate, objectToWorld.GetInverted());
		const bool addedToProxy = gSDK->AddObjectToContainer(duplicate, fhObject);
		WriteRuntimeTrace(std::string("recalculate add-to-proxy=") + (addedToProxy ? "true " : "false ") + DescribeObject(duplicate));
		if (!addedToProxy) {
			gSDK->DeleteObject(duplicate);
		}
	}
	else {
		WriteRuntimeTrace("recalculate duplicate failed");
	}

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
	outvecTypes.clear();

	SSourceBounds bounds;
	if (!GetSourceBounds(fhObject, bounds)) {
		WriteRuntimeTrace("supported-types bounds failed view=" + std::to_string(static_cast<int>(inView)));
		return false;
	}

	double width = 0.0;
	double height = 0.0;
	double depth = 0.0;
	GetViewLengths(inView, bounds, width, height, depth);
	constexpr double kMinimumDimension = 1e-6;

	if (width > kMinimumDimension) {
		AddSupportedType(outvecTypes, kOverallWidth, "Overall Width", VectorWorks::Extension::EAutoDimensionPlacement::eHorizontalBottom);
	}
	if (height > kMinimumDimension) {
		AddSupportedType(outvecTypes, kOverallHeight, "Overall Height", VectorWorks::Extension::EAutoDimensionPlacement::eVerticalRight);
	}
	if (depth > kMinimumDimension) {
		AddSupportedType(outvecTypes, kOverallDepth, "Overall Depth", VectorWorks::Extension::EAutoDimensionPlacement::eHorizontalTop);
	}

	std::ostringstream trace;
	trace.precision(12);
	trace << "supported-types view=" << static_cast<int>(inView)
		<< " width=" << width << " height=" << height << " depth=" << depth
		<< " count=" << outvecTypes.size();
	WriteRuntimeTrace(trace.str());

	return !outvecTypes.empty();
}

bool CAutoDimensionObj_EventSink::OnAutoDimMessage_GetDimensionDefinitions(EViewTypes inView, const TXString& instrDimID_UniversalName, VectorWorks::Extension::EAutoDimensionPlacement inPlace, std::vector<VectorWorks::Extension::TAutoDimensionDefinition>& outvecDimensions)
{
	(void)inPlace;
	outvecDimensions.clear();

	SSourceBounds bounds;
	WorldPt3 start;
	WorldPt3 end;
	if (!GetSourceBounds(fhObject, bounds) || !GetDimensionPoints(inView, instrDimID_UniversalName, bounds, start, end)) {
		WriteRuntimeTrace(std::string("dimension-definition failed id=") + GetDimensionTraceName(instrDimID_UniversalName)
			+ " view=" + std::to_string(static_cast<int>(inView)));
		return false;
	}

	outvecDimensions.emplace_back(start, end, 0);
	WriteRuntimeTrace(std::string("dimension-definition id=") + GetDimensionTraceName(instrDimID_UniversalName)
		+ " view=" + std::to_string(static_cast<int>(inView))
		+ " placement=" + std::to_string(static_cast<int>(inPlace))
		+ " start=" + FormatPoint(start) + " end=" + FormatPoint(end));
	return true;
}

bool CAutoDimensionObjDefTool_EventSink::DoSetUp(bool bRestore, const IToolModeBarInitProvider* pModeBarInitProvider)
{
	fSelectionProxyCreated = false;
	const bool result = VWTool_EventSink::DoSetUp(bRestore, pModeBarInitProvider);
	MCObjectHandle selectedObject = gSDK->FirstSelectedObject();
	if (IsSupportedSource(selectedObject)) {
		fSelectionProxyCreated = CreateLinkedProxy(selectedObject) != nullptr;
		WriteRuntimeTrace(std::string("tool-setup selection-proxy=") + (fSelectionProxyCreated ? "true" : "false"));
	}
	return result;
}

void CAutoDimensionObjDefTool_EventSink::DoSetDown(bool bRestore, const IToolModeBarInitProvider* pModeBarInitProvider)
{
	VWTool_EventSink::DoSetDown(bRestore, pModeBarInitProvider);
	fSelectionProxyCreated = false;
}

void CAutoDimensionObjDefTool_EventSink::HandleComplete()
{
	if (fSelectionProxyCreated) {
		WriteRuntimeTrace("tool-complete skipped; selection proxy already created");
		return;
	}

	MCObjectHandle sourceObject = nullptr;
	short overPart = 0;
	SintptrT code = 0;
	::GS_TrackTool(gCBP, sourceObject, overPart, code);
	WriteRuntimeTrace("tool-complete track overPart=" + std::to_string(overPart)
		+ " code=" + std::to_string(code) + " " + DescribeObject(sourceObject));
	if (!sourceObject) {
		sourceObject = gSDK->FirstSelectedObject();
		WriteRuntimeTrace("tool-complete selection fallback " + DescribeObject(sourceObject));
	}

	if (!IsSupportedSource(sourceObject)) {
		WriteRuntimeTrace("tool-complete rejected source");
		gSDK->AlertInform("Click a symbol, lighting device, line, 2D object, or 3D object.");
		return;
	}

	MCObjectHandle proxyObject = CreateLinkedProxy(sourceObject);
	if (!proxyObject) {
		WriteRuntimeTrace("tool-complete proxy creation failed");
		gSDK->AlertInform("The selected object could not be linked.");
	}
}
