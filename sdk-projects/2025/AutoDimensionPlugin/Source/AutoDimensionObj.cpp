#include "StdAfx.h"

#include "AutoDimensionObj.h"
#include "../../../../include/vwad/SDKComplexGeometry.h"
#include "../../../../include/vwad/SDKViewPlane.h"

#include <algorithm>
#include <array>
#include <cmath>
#if defined(_DEBUG)
#include <cstdlib>
#include <fstream>
#endif
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

using namespace AutoDimensionPlugin;

namespace AutoDimensionPlugin
{
	static const TXString kCategoryUniversalName = "KeeplAutoDimTestCategory";
	static const TXString kOverallWidth = "AD_OverallWidth";
	static const TXString kOverallHeight = "AD_OverallHeight";
	static const TXString kOverallDepth = "AD_OverallDepth";
	static const TXString kSourceUUIDParam = "SourceUUID";

	static constexpr short kLinearDimensionTypeOrtho = 0;
	static constexpr short kLinearDimensionTypeAligned = 1;
	static constexpr size_t kAnnotationModeGroup = 0;
	static constexpr size_t kEditModeGroup = 1;
	static constexpr size_t kSettingsButtonGroup = 2;
	static constexpr size_t kAnnotationAuto = 0;
	static constexpr size_t kAnnotationContinuous = 1;
	static constexpr size_t kAnnotationLine = 2;
	static constexpr size_t kAnnotationManualBlock = 3;
	static constexpr size_t kAnnotationIntersection = 4;
	static constexpr size_t kAnnotationSelection = 5;
	static constexpr size_t kAnnotationCenters = 6;
	static constexpr size_t kAnnotationBoundaries = 7;
	static constexpr size_t kAnnotationClosedSpace = 8;
	static constexpr size_t kAnnotationEnhanced = 9;
	static constexpr size_t kAnnotationQuickChain = 10;
	static constexpr size_t kEditNone = 0;
	static constexpr size_t kEditConvert = 1;
	static constexpr size_t kEditTrim = 2;
	static constexpr size_t kEditAlign = 3;
	static constexpr size_t kEditSplitExtend = 4;
	static constexpr size_t kEditTextDirection = 5;
	static constexpr size_t kEditPoints = 6;
	static constexpr size_t kEditMerge = 7;
	static constexpr size_t kEditAvoid = 8;
	static constexpr size_t kEditResetText = 9;
	static constexpr size_t kEditResetTextPosition = 10;
	static constexpr double kGeometryTolerance = 1e-6;
	static constexpr double kPi = 3.14159265358979323846;
	// W5 settings: persisted via Saved Settings (category "KeeplAutoDim"). Every key is
	// optional; when a key is missing the matching default (the historical hardcoded
	// value) is kept, so there is no migration.
	namespace
	{
		static const TXString kSettingsCategory = "KeeplAutoDim";

		struct SAutoDimSettings {
			WorldCoord dimOffsetBase              = 25.0;
			double     dimOffsetRatio             = 0.15;
			WorldCoord avoidStep                  = 15.0;
			size_t     avoidIterations            = 12;
			WorldCoord intersectionDedupTolerance = 1e-4;
		};

		static void NormalizeAutoDimSettings(SAutoDimSettings& settings)
		{
			// Saved settings are user-editable. Keep malformed values from reaching
			// offset, collision, or intersection calculations.
			const SAutoDimSettings defaults;
			const auto finiteNonNegative = [](double value, double fallback, double maximum) {
				return std::isfinite(value) ? std::clamp(value, 0.0, maximum) : fallback;
			};
			settings.dimOffsetBase = finiteNonNegative(settings.dimOffsetBase, defaults.dimOffsetBase, 1.0e6);
			settings.dimOffsetRatio = finiteNonNegative(settings.dimOffsetRatio, defaults.dimOffsetRatio, 10.0);
			settings.avoidStep = finiteNonNegative(settings.avoidStep, defaults.avoidStep, 1.0e6);
			if (settings.avoidIterations == 0) settings.avoidIterations = defaults.avoidIterations;
			settings.avoidIterations = std::min<size_t>(settings.avoidIterations, 1000);
			settings.intersectionDedupTolerance = finiteNonNegative(settings.intersectionDedupTolerance, defaults.intersectionDedupTolerance, 1.0e6);
			if (settings.intersectionDedupTolerance < 1.0e-9) settings.intersectionDedupTolerance = 1.0e-9;
		}

		// Saved Settings are persisted as text, so they must round-trip independently of
		// the host locale. std::to_string / std::stod follow the global locale, which
		// writes "0,15" and reads it back as 0 wherever the decimal separator is a comma.
		// Both directions are pinned to the classic locale instead.
		static TXString ToSettingString(double value)
		{
			std::ostringstream output;
			output.imbue(std::locale::classic());
			output.precision(17);
			output << value;
			return TXString(output.str());
		}

		static TXString ToSettingString(size_t value)
		{
			std::ostringstream output;
			output.imbue(std::locale::classic());
			output << value;
			return TXString(output.str());
		}

		static bool ParseSettingDouble(const TXString& value, double& outValue)
		{
			std::istringstream input(value.GetStdString());
			input.imbue(std::locale::classic());
			double parsed = 0.0;
			if (!(input >> parsed) || !std::isfinite(parsed)) return false;
			input >> std::ws;
			if (!input.eof()) return false;
			outValue = parsed;
			return true;
		}

		static bool ParseSettingSize(const TXString& value, size_t& outValue)
		{
			std::istringstream input(value.GetStdString());
			input.imbue(std::locale::classic());
			unsigned long long parsed = 0;
			if (!(input >> parsed)) return false;
			input >> std::ws;
			if (!input.eof() || parsed > static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) return false;
			outValue = static_cast<size_t>(parsed);
			return true;
		}

		static SAutoDimSettings GetAutoDimSettings()
		{
			SAutoDimSettings settings;
			TXString savedValue;
			if (gSDK->GetSavedSetting(kSettingsCategory, "DimOffsetBase", savedValue)) {
				double parsed = 0.0;
				if (ParseSettingDouble(savedValue, parsed)) settings.dimOffsetBase = parsed;
			}
			if (gSDK->GetSavedSetting(kSettingsCategory, "DimOffsetRatio", savedValue)) {
				double parsed = 0.0;
				if (ParseSettingDouble(savedValue, parsed)) settings.dimOffsetRatio = parsed;
			}
			if (gSDK->GetSavedSetting(kSettingsCategory, "AvoidStep", savedValue)) {
				double parsed = 0.0;
				if (ParseSettingDouble(savedValue, parsed)) settings.avoidStep = parsed;
			}
			if (gSDK->GetSavedSetting(kSettingsCategory, "AvoidIterations", savedValue)) {
				size_t parsed = 0;
				if (ParseSettingSize(savedValue, parsed) && parsed > 0) settings.avoidIterations = parsed;
			}
			if (gSDK->GetSavedSetting(kSettingsCategory, "IntersectionDedupTolerance", savedValue)) {
				double parsed = 0.0;
				if (ParseSettingDouble(savedValue, parsed)) settings.intersectionDedupTolerance = parsed;
			}
			NormalizeAutoDimSettings(settings);
			return settings;
		}

		static void SaveAutoDimSettings(const SAutoDimSettings& settings)
		{
			SAutoDimSettings normalized = settings;
			NormalizeAutoDimSettings(normalized);
			gSDK->SetSavedSetting(kSettingsCategory, "DimOffsetBase", ToSettingString(normalized.dimOffsetBase));
			gSDK->SetSavedSetting(kSettingsCategory, "DimOffsetRatio", ToSettingString(normalized.dimOffsetRatio));
			gSDK->SetSavedSetting(kSettingsCategory, "AvoidStep", ToSettingString(normalized.avoidStep));
			gSDK->SetSavedSetting(kSettingsCategory, "AvoidIterations", ToSettingString(normalized.avoidIterations));
			gSDK->SetSavedSetting(kSettingsCategory, "IntersectionDedupTolerance", ToSettingString(normalized.intersectionDedupTolerance));
		}

		// W5 settings dialog: pure-code layout (no dialog resource), following the
		// VWComplexDialog skeleton from the SDK "VWUI Dialogs Sample".
		static constexpr TControlID kDimOffsetBaseLabelID              = 8;
		static constexpr TControlID kDimOffsetRatioLabelID             = 9;
		static constexpr TControlID kAvoidStepLabelID                  = 10;
		static constexpr TControlID kAvoidIterationsLabelID            = 11;
		static constexpr TControlID kIntersectionDedupToleranceLabelID = 12;
		static constexpr TControlID kDimOffsetBaseCtrlID               = 3;
		static constexpr TControlID kDimOffsetRatioCtrlID              = 4;
		static constexpr TControlID kAvoidStepCtrlID                   = 5;
		static constexpr TControlID kAvoidIterationsCtrlID             = 6;
		static constexpr TControlID kIntersectionDedupToleranceCtrlID  = 7;

		class CAutoDimSettingsDlg : public VWComplexDialog
		{
		public:
			CAutoDimSettingsDlg();
			virtual ~CAutoDimSettingsDlg();

			SAutoDimSettings GetSettings() const;

		protected:
			virtual bool CreateDialogLayout();
			virtual void OnInitializeContent();
			virtual void OnDDXInitialize();

		protected:
			DEFINE_EVENT_DISPATH_MAP;

		protected:
			VWStaticTextCtrl fDimOffsetBaseLabel;
			VWStaticTextCtrl fDimOffsetRatioLabel;
			VWStaticTextCtrl fAvoidStepLabel;
			VWStaticTextCtrl fAvoidIterationsLabel;
			VWStaticTextCtrl fIntersectionDedupToleranceLabel;
			VWEditRealCtrl fDimOffsetBaseCtrl;
			VWEditRealCtrl fDimOffsetRatioCtrl;
			VWEditRealCtrl fAvoidStepCtrl;
			VWEditIntegerCtrl fAvoidIterationsCtrl;
			VWEditRealCtrl fIntersectionDedupToleranceCtrl;

			SAutoDimSettings fSettings;
			double fDimOffsetBase;
			double fDimOffsetRatio;
			double fAvoidStep;
			Sint32 fAvoidIterations;
			double fIntersectionDedupTolerance;
		};

		CAutoDimSettingsDlg::CAutoDimSettingsDlg() :
			fDimOffsetBaseLabel(kDimOffsetBaseLabelID),
			fDimOffsetRatioLabel(kDimOffsetRatioLabelID),
			fAvoidStepLabel(kAvoidStepLabelID),
			fAvoidIterationsLabel(kAvoidIterationsLabelID),
			fIntersectionDedupToleranceLabel(kIntersectionDedupToleranceLabelID),
			fDimOffsetBaseCtrl(kDimOffsetBaseCtrlID),
			fDimOffsetRatioCtrl(kDimOffsetRatioCtrlID),
			fAvoidStepCtrl(kAvoidStepCtrlID),
			fAvoidIterationsCtrl(kAvoidIterationsCtrlID),
			fIntersectionDedupToleranceCtrl(kIntersectionDedupToleranceCtrlID),
			fDimOffsetBase(25.0),
			fDimOffsetRatio(0.15),
			fAvoidStep(15.0),
			fAvoidIterations(12),
			fIntersectionDedupTolerance(1e-4)
		{
		}

		CAutoDimSettingsDlg::~CAutoDimSettingsDlg()
		{
		}

		bool CAutoDimSettingsDlg::CreateDialogLayout()
		{
			if (!CreateDialog("标注设置", "确定", "取消", true)) {
				return false;
			}

			if (!fDimOffsetBaseLabel.CreateControl(this, "尺寸偏移基数：")) return false;
			if (!fDimOffsetRatioLabel.CreateControl(this, "通用偏移比例：")) return false;
			if (!fAvoidStepLabel.CreateControl(this, "文字避让步长：")) return false;
			if (!fAvoidIterationsLabel.CreateControl(this, "文字避让迭代上限：")) return false;
			if (!fIntersectionDedupToleranceLabel.CreateControl(this, "交线去重阈值：")) return false;

			if (!fDimOffsetBaseCtrl.CreateControl(this, 25.0, 8, VWEditRealCtrl::kEditControlDimension)) return false;
			if (!fDimOffsetRatioCtrl.CreateControl(this, 0.15, 8, VWEditRealCtrl::kEditControlReal)) return false;
			if (!fAvoidStepCtrl.CreateControl(this, 15.0, 8, VWEditRealCtrl::kEditControlDimension)) return false;
			if (!fAvoidIterationsCtrl.CreateControl(this, 12, 8)) return false;
			if (!fIntersectionDedupToleranceCtrl.CreateControl(this, 1e-4, 8, VWEditRealCtrl::kEditControlReal)) return false;

			AddFirstGroupControl(&fDimOffsetBaseLabel);
			AddRightControl(&fDimOffsetBaseLabel, &fDimOffsetBaseCtrl);
			AddBelowControl(&fDimOffsetBaseLabel, &fDimOffsetRatioLabel);
			AddRightControl(&fDimOffsetRatioLabel, &fDimOffsetRatioCtrl);
			AddBelowControl(&fDimOffsetRatioLabel, &fAvoidStepLabel);
			AddRightControl(&fAvoidStepLabel, &fAvoidStepCtrl);
			AddBelowControl(&fAvoidStepLabel, &fAvoidIterationsLabel);
			AddRightControl(&fAvoidIterationsLabel, &fAvoidIterationsCtrl);
			AddBelowControl(&fAvoidIterationsLabel, &fIntersectionDedupToleranceLabel);
			AddRightControl(&fIntersectionDedupToleranceLabel, &fIntersectionDedupToleranceCtrl);

			return true;
		}

		void CAutoDimSettingsDlg::OnInitializeContent()
		{
			VWDialog::OnInitializeContent();

			fSettings = GetAutoDimSettings();
			fDimOffsetBase = fSettings.dimOffsetBase;
			fDimOffsetRatio = fSettings.dimOffsetRatio;
			fAvoidStep = fSettings.avoidStep;
			fAvoidIterations = static_cast<Sint32>(fSettings.avoidIterations);
			fIntersectionDedupTolerance = fSettings.intersectionDedupTolerance;

			fDimOffsetBaseCtrl.SetEditReal(fDimOffsetBase, VWEditRealCtrl::kEditControlDimension);
			fDimOffsetRatioCtrl.SetEditReal(fDimOffsetRatio, VWEditRealCtrl::kEditControlReal);
			fAvoidStepCtrl.SetEditReal(fAvoidStep, VWEditRealCtrl::kEditControlDimension);
			fAvoidIterationsCtrl.SetEditInteger(fAvoidIterations);
			fIntersectionDedupToleranceCtrl.SetEditReal(fIntersectionDedupTolerance, VWEditRealCtrl::kEditControlReal);
		}

		void CAutoDimSettingsDlg::OnDDXInitialize()
		{
			AddDDX_EditReal(kDimOffsetBaseCtrlID, &fDimOffsetBase, VWEditRealCtrl::kEditControlDimension);
			AddDDX_EditReal(kDimOffsetRatioCtrlID, &fDimOffsetRatio, VWEditRealCtrl::kEditControlReal);
			AddDDX_EditReal(kAvoidStepCtrlID, &fAvoidStep, VWEditRealCtrl::kEditControlDimension);
			AddDDX_EditInteger(kAvoidIterationsCtrlID, &fAvoidIterations);
			AddDDX_EditReal(kIntersectionDedupToleranceCtrlID, &fIntersectionDedupTolerance, VWEditRealCtrl::kEditControlReal);
		}

		EVENT_DISPATCH_MAP_BEGIN(CAutoDimSettingsDlg);
		EVENT_DISPATCH_MAP_END;

		SAutoDimSettings CAutoDimSettingsDlg::GetSettings() const
		{
			SAutoDimSettings settings = fSettings;
			settings.dimOffsetBase = fDimOffsetBase;
			settings.dimOffsetRatio = fDimOffsetRatio;
			settings.avoidStep = fAvoidStep;
			settings.avoidIterations = (fAvoidIterations > 0) ? static_cast<size_t>(fAvoidIterations) : 1;
			settings.intersectionDedupTolerance = fIntersectionDedupTolerance;
			NormalizeAutoDimSettings(settings);
			return settings;
		}

		static void OpenSettingsDialog()
		{
			CAutoDimSettingsDlg dlg;
			if (dlg.RunDialogLayout("") == kDialogButton_Ok) {
				SaveAutoDimSettings(dlg.GetSettings());
			}
		}
	}

	#if defined(_DEBUG)
	static std::string GetDebugTracePath(const char* fileName)
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

	static void WriteRuntimeTrace(const std::string& message)
	{
		std::ofstream trace(GetDebugTracePath("vw-autodim-runtime-2025.txt"), std::ios::app);
		if (trace.is_open()) {
			trace << message << '\n';
		}
	}
	#define VWAD_RUNTIME_TRACE(message) WriteRuntimeTrace(message)

	static std::string FormatPoint(const WorldPt3& point)
	{
		std::ostringstream output;
		output.precision(12);
		output << '(' << point.x << ',' << point.y << ',' << point.z << ')';
		return output.str();
	}

	static std::string FormatPoint(const WorldPt& point)
	{
		std::ostringstream output;
		output.precision(12);
		output << '(' << point.x << ',' << point.y << ')';
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
	#else
	#define VWAD_RUNTIME_TRACE(message) do { } while (false)
	#endif

	static const char* GetDimensionTraceName(const TXString& dimensionID)
	{
		if (dimensionID == kOverallWidth) return "OverallWidth";
		if (dimensionID == kOverallHeight) return "OverallHeight";
		if (dimensionID == kOverallDepth) return "OverallDepth";
		return "Unknown";
	}

	static size_t CreateDimensionsForSource(MCObjectHandle sourceObject, const ViewPlane::SViewPlane& plane);
	static WorldCube GetSourcesCube(const std::vector<MCObjectHandle>& sources);

	static bool IsSupportedSource(MCObjectHandle object)
	{
		return object &&
			gSDK->GetObjectTypeN(object) != dimHeaderNode &&
			!VWParametricObj::IsParametricObject(object, "KeeplAutoDimTestObj");
	}

	static bool IsClosedSpaceSource(MCObjectHandle object)
	{
		if (!object) return false;
		const short type = gSDK->GetObjectTypeN(object);
		return (type == kPolygonNode || type == kPolylineNode || type == kFreehandPolygonNode) && gSDK->GetPolyShapeClose(object);
	}

	struct SLineMeasurement
	{
		WorldPt start;
		WorldPt end;
		WorldCoord dx = 0.0;
		WorldCoord dy = 0.0;
		double length = 0.0;
		double angleDegrees = 0.0;
	};

	static bool BuildLineMeasurement(const WorldPt& start, const WorldPt& end, SLineMeasurement& outMeasurement)
	{
		outMeasurement.start = start;
		outMeasurement.end = end;
		outMeasurement.dx = end.x - start.x;
		outMeasurement.dy = end.y - start.y;
		outMeasurement.length = std::hypot(outMeasurement.dx, outMeasurement.dy);
		if (outMeasurement.length <= kGeometryTolerance) {
			return false;
		}

		outMeasurement.angleDegrees = std::atan2(
			std::abs(outMeasurement.dy),
			std::abs(outMeasurement.dx)) * 180.0 / kPi;
		return true;
	}

	static bool GetLineMeasurement(MCObjectHandle object, SLineMeasurement& outMeasurement)
	{
		if (!object || gSDK->GetObjectTypeN(object) != kLineNode) {
			return false;
		}

		WorldPt start;
		WorldPt end;
		gSDK->GetEndPoints(object, start, end);
		return BuildLineMeasurement(start, end, outMeasurement);
	}

	static bool GetLineAngleDefinition(const SLineMeasurement& line, WorldCoord referenceLength, WorldPt& outCenter, WorldPt& outP1, WorldPt& outP2)
	{
		outCenter = line.start;
		WorldPt otherPoint = line.end;
		if (otherPoint.x < outCenter.x ||
			(std::abs(otherPoint.x - outCenter.x) <= kGeometryTolerance && otherPoint.y < outCenter.y)) {
			std::swap(outCenter, otherPoint);
		}

		const WorldCoord dx = otherPoint.x - outCenter.x;
		const WorldCoord dy = otherPoint.y - outCenter.y;
		if (std::abs(dx) <= kGeometryTolerance && std::abs(dy) <= kGeometryTolerance) {
			return false;
		}

		const WorldCoord horizontalLength = std::max<WorldCoord>(std::abs(dx), referenceLength);
		const WorldPt horizontalPoint(outCenter.x + horizontalLength, outCenter.y);
		if (std::abs(dy) <= kGeometryTolerance) {
			return false;
		}

		if (dy > 0.0) {
			outP1 = horizontalPoint;
			outP2 = otherPoint;
		}
		else {
			outP1 = otherPoint;
			outP2 = horizontalPoint;
		}

		// CreateAngleDimension follows the ordered rays. Keep the minor angle when
		// the line points below the reference ray or crosses the -180/180 boundary.
		const double angle1 = std::atan2(outP1.y - outCenter.y, outP1.x - outCenter.x);
		const double angle2 = std::atan2(outP2.y - outCenter.y, outP2.x - outCenter.x);
		double sweep = angle2 - angle1;
		while (sweep < 0.0) sweep += 2.0 * kPi;
		while (sweep >= 2.0 * kPi) sweep -= 2.0 * kPi;
		if (sweep > kPi) {
			std::swap(outP1, outP2);
		}

		return true;
	}

	static void ApplyDimensionPresentation(MCObjectHandle dimension, const ViewPlane::SViewPlane& plane, const char* traceName)
	{
		// The planar reference has to be applied before the reset so the dimension is
		// laid out on the measuring plane instead of the ground plane.
		ViewPlane::ApplyPlanarRef(dimension, plane);
		gSDK->ResetObject(dimension);
		VWAD_RUNTIME_TRACE(std::string("dimension-tool presentation ") + traceName
			+ " plane=" + plane.name);
	}

	static MCObjectHandle AddLinearDimension(const WorldPt& p1, const WorldPt& p2, WorldCoord startOffset, const Vector2& direction, short dimensionType, const char* traceName, const ViewPlane::SViewPlane& plane, size_t& ioCreatedCount)
	{
		MCObjectHandle dimension = gSDK->CreateLinearDimension(p1, p2, startOffset, 0.0, direction, dimensionType);
		if (dimension) {
			ApplyDimensionPresentation(dimension, plane, traceName);
			// The dimension is already in the drawing; AddAfterSwapObject only records the
			// undo primitive. Losing undo bookkeeping must not discard a valid dimension.
			if (!gSDK->AddAfterSwapObject(dimension)) {
				VWAD_RUNTIME_TRACE(std::string("dimension-tool undo registration failed ") + traceName);
			}
			++ioCreatedCount;
			VWAD_RUNTIME_TRACE(std::string("dimension-tool created ") + traceName + " " + DescribeObject(dimension));
		}
		else {
			VWAD_RUNTIME_TRACE(std::string("dimension-tool failed ") + traceName);
		}
		return dimension;
	}

	static MCObjectHandle AddAngleDimension(const WorldPt& center, const WorldPt& p1, const WorldPt& p2, WorldCoord startOffset, const char* traceName, const ViewPlane::SViewPlane& plane, size_t& ioCreatedCount)
	{
		MCObjectHandle dimension = gSDK->CreateAngleDimension(center, p1, p2, startOffset);
		if (dimension) {
			ApplyDimensionPresentation(dimension, plane, traceName);
			// The dimension is already in the drawing; AddAfterSwapObject only records the
			// undo primitive. Losing undo bookkeeping must not discard a valid dimension.
			if (!gSDK->AddAfterSwapObject(dimension)) {
				VWAD_RUNTIME_TRACE(std::string("dimension-tool undo registration failed ") + traceName);
			}
			++ioCreatedCount;
			VWAD_RUNTIME_TRACE(std::string("dimension-tool created ") + traceName + " " + DescribeObject(dimension));
		}
		else {
			VWAD_RUNTIME_TRACE(std::string("dimension-tool failed ") + traceName);
		}
		return dimension;
	}

	static MCObjectHandle AddCircularDimension(const WorldPt& center, const WorldPt& end, WorldCoord startOffset, bool radius, const char* traceName, const ViewPlane::SViewPlane& plane, size_t& ioCreatedCount)
	{
		const WorldRect box(center, std::hypot(end.x - center.x, end.y - center.y));
		MCObjectHandle dimension = gSDK->CreateCircularDimension(center, end, startOffset, 0.0, box, radius);
		if (dimension) {
			ApplyDimensionPresentation(dimension, plane, traceName);
			// The dimension is already in the drawing; AddAfterSwapObject only records the
			// undo primitive. Losing undo bookkeeping must not discard a valid dimension.
			if (!gSDK->AddAfterSwapObject(dimension)) {
				VWAD_RUNTIME_TRACE(std::string("dimension-tool undo registration failed ") + traceName);
			}
			++ioCreatedCount;
			VWAD_RUNTIME_TRACE(std::string("dimension-tool created ") + traceName + " " + DescribeObject(dimension));
		}
		return dimension;
	}

	static MCObjectHandle AddArcLengthDimension(const WorldPt& start, const WorldPt& end, const WorldPt& center, WorldCoord startOffset, bool clockwise, const char* traceName, const ViewPlane::SViewPlane& plane, size_t& ioCreatedCount)
	{
		MCObjectHandle dimension = gSDK->CreateArcLengthDimension(start, end, center, startOffset, clockwise, false, true);
		if (dimension) {
			ApplyDimensionPresentation(dimension, plane, traceName);
			// The dimension is already in the drawing; AddAfterSwapObject only records the
			// undo primitive. Losing undo bookkeeping must not discard a valid dimension.
			if (!gSDK->AddAfterSwapObject(dimension)) {
				VWAD_RUNTIME_TRACE(std::string("dimension-tool undo registration failed ") + traceName);
			}
			++ioCreatedCount;
			VWAD_RUNTIME_TRACE(std::string("dimension-tool created ") + traceName + " " + DescribeObject(dimension));
		}
		return dimension;
	}

	static bool GetCircularGeometry(MCObjectHandle object, WorldPt& outCenter, WorldPt& outStart, WorldPt& outEnd, bool& outClockwise)
	{
		if (!object) return false;
		const short type = gSDK->GetObjectTypeN(object);
		if (type != kArcNode) return false;

		double startAngle = 0.0;
		double sweepAngle = 0.0;
		WorldCoord radiusX = 0.0;
		WorldCoord radiusY = 0.0;
		gSDK->GetArcInfoN(object, startAngle, sweepAngle, outCenter, radiusX, radiusY);
		if (std::abs(radiusX - radiusY) > kGeometryTolerance || std::abs(radiusX) <= kGeometryTolerance) return false;

		const double startRadians = startAngle * kPi / 180.0;
		const double endRadians = (startAngle + sweepAngle) * kPi / 180.0;
		outStart = WorldPt(outCenter.x + radiusX * std::cos(startRadians), outCenter.y + radiusY * std::sin(startRadians));
		outEnd = WorldPt(outCenter.x + radiusX * std::cos(endRadians), outCenter.y + radiusY * std::sin(endRadians));
		outClockwise = sweepAngle < 0.0;
		return true;
	}

	static bool GetDimensionPoint(MCObjectHandle object, short selector, WorldPt& outPoint)
	{
		TVariableBlock value;
		return object && gSDK->GetObjectVariable(object, selector, value) && value.GetWorldPt(outPoint);
	}

	static bool GetDimensionReal(MCObjectHandle object, short selector, double& outValue)
	{
		TVariableBlock value;
		return object && gSDK->GetObjectVariable(object, selector, value) && value.GetReal64(outValue);
	}

	static bool GetDimensionShort(MCObjectHandle object, short selector, Sint16& outValue)
	{
		TVariableBlock value;
		return object && gSDK->GetObjectVariable(object, selector, value) && value.GetSint16(outValue);
	}

	static bool GetDimensionUnsignedChar(MCObjectHandle object, short selector, Uint8& outValue)
	{
		TVariableBlock value;
		return object && gSDK->GetObjectVariable(object, selector, value) && value.GetUint8(outValue);
	}

	static bool GetDimensionBool(MCObjectHandle object, short selector, bool& outValue)
	{
		TVariableBlock value;
		return object && gSDK->GetObjectVariable(object, selector, value) && value.GetBoolean(outValue);
	}

	static bool GetDimensionString(MCObjectHandle object, short selector, TXString& outValue)
	{
		TVariableBlock value;
		return object && gSDK->GetObjectVariable(object, selector, value) && value.GetTXString(outValue);
	}

	static WorldCoord CurrentUnitsToWorld(WorldCoord value)
	{
		double_gs worldValue = 0.0;
		gSDK->UnitsLengthToFPCoordLength(value, worldValue);
		return worldValue;
	}

	static std::vector<MCObjectHandle> CollectSelectedDimensions()
	{
		std::vector<MCObjectHandle> dimensions;
		VWFC::VWObjects::VWObjectIterator iterator(gSDK->FirstSelectedObject());
		while (iterator) {
			MCObjectHandle object = *iterator;
			if (object && gSDK->GetObjectTypeN(object) == dimHeaderNode) dimensions.push_back(object);
			iterator.MoveNextSelected();
		}
		return dimensions;
	}

	struct SDimensionVariableChange
	{
		short selector;
		TVariableBlock value;
	};

	// Undo registration is best effort: AddBothSwapObject only records an undo
	// primitive, so losing it must not block the edit itself.
	static void RegisterDimensionForUndo(MCObjectHandle dimension, std::vector<MCObjectHandle>& ioRegistered)
	{
		if (!dimension || std::find(ioRegistered.begin(), ioRegistered.end(), dimension) != ioRegistered.end()) return;
		if (gSDK->AddBothSwapObject(dimension)) ioRegistered.push_back(dimension);
		else VWAD_RUNTIME_TRACE("dimension-edit undo registration failed " + DescribeObject(dimension));
	}

	// Restores the values it managed to read when the SDK rejects a later write, so a
	// partial write cannot leave the dimension inconsistent. A selector whose previous
	// value cannot be read is still written; it just cannot participate in a rollback.
	static bool ApplyDimensionVariableTransaction(MCObjectHandle dimension, const std::vector<SDimensionVariableChange>& changes, std::vector<MCObjectHandle>& ioUndoRegistered)
	{
		if (!dimension || changes.empty() || gSDK->GetObjectTypeN(dimension) != dimHeaderNode) return false;
		std::vector<TVariableBlock> originalValues(changes.size());
		std::vector<bool> restorable(changes.size(), false);
		for (size_t index = 0; index < changes.size(); ++index) {
			restorable[index] = gSDK->GetObjectVariable(dimension, changes[index].selector, originalValues[index]);
		}
		RegisterDimensionForUndo(dimension, ioUndoRegistered);
		for (size_t index = 0; index < changes.size(); ++index) {
			if (gSDK->SetObjectVariable(dimension, changes[index].selector, changes[index].value)) continue;
			for (size_t restoreIndex = 0; restoreIndex < index; ++restoreIndex) {
				if (restorable[restoreIndex]) gSDK->SetObjectVariable(dimension, changes[restoreIndex].selector, originalValues[restoreIndex]);
			}
			gSDK->ResetObject(dimension);
			VWAD_RUNTIME_TRACE("dimension-edit write rejected selector=" + std::to_string(changes[index].selector));
			return false;
		}
		gSDK->ResetObject(dimension);
		return true;
	}

	// Copies presentation on a best-effort basis. Individual selectors are optional: a
	// replacement dimension of a different class may legitimately reject some of them,
	// and failing the whole operation over one cosmetic variable would abort the caller's
	// convert/split/merge and discard an otherwise valid dimension.
	static bool CopyDimensionPresentationFrom(MCObjectHandle source, MCObjectHandle target)
	{
		if (!source || !target) return false;
		// Preserve public presentation state only. Geometry class is established by
		// CreateLinearDimension/CreateChainDimension and must not be overwritten.
		const std::array<short, 17> selectors = {
			ovDimStartOffset,
			ovDimStartOffsetInCurrUnits,
			ovDimWitnessOverride,
			ovDimCustStartWitOffset,
			ovDimCustEndWitOffset,
			ovDimTextAboveLineInCurrUnits,
			ovDimTextOffsetInCurrUnits,
			ovDimTextPosCalculated,
			ovDimTextPosInside,
			ovDimTextRotation,
			ovDimTextSizeInPoints,
			ovDimLeaderToLeft,
			ovDimLeaderText,
			ovDimTrailerText,
			ovDimNoteText,
			ovDimLeaderText2,
			ovDimTrailerText2,
		};
		size_t copiedCount = 0;
		for (short selector : selectors) {
			TVariableBlock value;
			if (gSDK->GetObjectVariable(source, selector, value) && gSDK->SetObjectVariable(target, selector, value)) ++copiedCount;
		}
		const InternalIndex sourceClass = gSDK->GetObjectClass(source);
		if (sourceClass > 0) gSDK->SetObjectClass(target, sourceClass);
		gSDK->ResetObject(target);
		VWAD_RUNTIME_TRACE("dimension-presentation copied=" + std::to_string(copiedCount)
			+ "/" + std::to_string(selectors.size()));
		return true;
	}

	static bool SwapDimensionEndpointsAndWitnessOffsets(MCObjectHandle dimension, const WorldPt& start, const WorldPt& end, std::vector<MCObjectHandle>& ioUndoRegistered)
	{
		double startWitnessOffset = 0.0;
		double endWitnessOffset = 0.0;
		if (!GetDimensionReal(dimension, ovDimCustStartWitOffset, startWitnessOffset) ||
			!GetDimensionReal(dimension, ovDimCustEndWitOffset, endWitnessOffset)) return false;
		return ApplyDimensionVariableTransaction(dimension, {
			{ ovDimStartPt, TVariableBlock(end) },
			{ ovDimEndPt, TVariableBlock(start) },
			{ ovDimCustStartWitOffset, TVariableBlock(static_cast<Real64>(endWitnessOffset)) },
			{ ovDimCustEndWitOffset, TVariableBlock(static_cast<Real64>(startWitnessOffset)) },
		}, ioUndoRegistered);
	}

	static size_t CreateEnhancedDimensionsForSource(MCObjectHandle sourceObject, const ViewPlane::SViewPlane& plane)
	{
		const SAutoDimSettings s = GetAutoDimSettings();
		size_t createdCount = 0;
		const short type = sourceObject ? gSDK->GetObjectTypeN(sourceObject) : 0;

		// Handle circular arcs
		if (type == kArcNode) {
			WorldPt center;
			WorldPt start;
			WorldPt end;
			bool clockwise = false;
			if (GetCircularGeometry(sourceObject, center, start, end, clockwise)) {
				WorldCube cube;
				gSDK->GetObjectCube(sourceObject, cube);
				const WorldCoord extent = std::max<WorldCoord>(cube.MaxX() - cube.MinX(), cube.MaxY() - cube.MinY());
				const WorldCoord offset = std::max<WorldCoord>(s.dimOffsetBase, extent * 0.25);
				gSDK->SetUndoMethod(kUndoSwapObjects);
				AddCircularDimension(center, end, offset, true, "radius", plane, createdCount);
				AddCircularDimension(center, end, offset * 1.7, false, "diameter", plane, createdCount);
				AddArcLengthDimension(start, end, center, offset * 2.4, clockwise, "arc-length", plane, createdCount);
				if (createdCount > 0) gSDK->EndUndoEvent();
				return createdCount;
			}
		}

		// Handle ovals (ellipses)
		if (type == kOvalNode) {
			double startAngle = 0.0;
			double sweepAngle = 0.0;
			WorldPt center;
			WorldCoord radiusX = 0.0;
			WorldCoord radiusY = 0.0;
			gSDK->GetArcInfoN(sourceObject, startAngle, sweepAngle, center, radiusX, radiusY);

			if (std::abs(radiusX) > kGeometryTolerance && std::abs(radiusY) > kGeometryTolerance) {
				const bool isCircle = std::abs(radiusX - radiusY) <= kGeometryTolerance;
				const WorldCoord maxRadius = std::max(radiusX, radiusY);
				const WorldCoord offset = std::max<WorldCoord>(s.dimOffsetBase, maxRadius * 0.25);

				gSDK->SetUndoMethod(kUndoSwapObjects);

				if (isCircle) {
					const WorldPt endPoint(center.x + radiusX, center.y);
					AddCircularDimension(center, endPoint, offset, true, "oval-radius", plane, createdCount);
					AddCircularDimension(center, endPoint, offset * 1.7, false, "oval-diameter", plane, createdCount);
				}
				else {
					// For ellipses, create aligned linear dimensions for major and minor axes
					const WorldPt majorStart(center.x - radiusX, center.y);
					const WorldPt majorEnd(center.x + radiusX, center.y);
					const WorldPt minorStart(center.x, center.y - radiusY);
					const WorldPt minorEnd(center.x, center.y + radiusY);

					AddLinearDimension(majorStart, majorEnd, offset, Vector2(1.0, 0.0), kLinearDimensionTypeAligned, "ellipse-major-axis", plane, createdCount);
					AddLinearDimension(minorStart, minorEnd, offset, Vector2(0.0, 1.0), kLinearDimensionTypeAligned, "ellipse-minor-axis", plane, createdCount);
				}

				if (createdCount > 0) gSDK->EndUndoEvent();
				VWAD_RUNTIME_TRACE("enhanced-oval isCircle=" + std::to_string(isCircle) + " radiusX=" + std::to_string(radiusX) + " radiusY=" + std::to_string(radiusY) + " created=" + std::to_string(createdCount));
				return createdCount;
			}
		}

		// Handle polylines with arc vertices
		if (type == kPolygonNode || type == kPolylineNode) {
			struct ArcEdgeData {
				WorldPt start;
				WorldPt end;
				WorldCoord radius;
				VertexType vType;
			};
			std::vector<ArcEdgeData> arcEdges;

			auto edgeCallback = [](const WorldPt& start, const WorldPt& control, const WorldPt& end, WorldCoord radius, VertexType vType, Sint8 visible, CallBackPtr, void* env) {
				if ((vType == vtArc || vType == vtRadius) && std::abs(radius) > kGeometryTolerance) {
					auto* edges = static_cast<std::vector<ArcEdgeData>*>(env);
					edges->push_back({start, end, radius, vType});
				}
			};

			gSDK->ForEachPolyEdge(sourceObject, edgeCallback, &arcEdges);

			if (!arcEdges.empty()) {
				WorldCube cube;
				gSDK->GetObjectCube(sourceObject, cube);
				const WorldCoord extent = std::max<WorldCoord>(cube.MaxX() - cube.MinX(), cube.MaxY() - cube.MinY());
				const WorldCoord baseOffset = std::max<WorldCoord>(s.dimOffsetBase, extent * s.dimOffsetRatio);

				gSDK->SetUndoMethod(kUndoSwapObjects);

				for (size_t i = 0; i < arcEdges.size() && i < 8; ++i) {
					const ArcEdgeData& arc = arcEdges[i];
					const WorldCoord dx = arc.end.x - arc.start.x;
					const WorldCoord dy = arc.end.y - arc.start.y;
					const WorldCoord chordLength = std::hypot(dx, dy);

					if (chordLength > kGeometryTolerance) {
						// Calculate arc center from start, end, and radius
						const WorldCoord midX = (arc.start.x + arc.end.x) * 0.5;
						const WorldCoord midY = (arc.start.y + arc.end.y) * 0.5;
						const WorldCoord perpX = -dy / chordLength;
						const WorldCoord perpY = dx / chordLength;
						const WorldCoord halfChord = chordLength * 0.5;
						const WorldCoord absRadius = std::abs(arc.radius);

						if (absRadius > halfChord) {
							const WorldCoord centerDist = std::sqrt(absRadius * absRadius - halfChord * halfChord);
							const WorldCoord sign = (arc.radius > 0.0) ? 1.0 : -1.0;
							const WorldPt center(midX + perpX * centerDist * sign, midY + perpY * centerDist * sign);
							const bool clockwise = arc.radius < 0.0;

							const WorldCoord offset = baseOffset * (1.0 + static_cast<WorldCoord>(i) * 0.3);
							AddCircularDimension(center, arc.end, offset, true, "polyline-arc-radius", plane, createdCount);
							AddArcLengthDimension(arc.start, arc.end, center, offset * 1.5, clockwise, "polyline-arc-length", plane, createdCount);
						}
					}
				}

				if (createdCount > 0) gSDK->EndUndoEvent();
				VWAD_RUNTIME_TRACE("enhanced-polyline-arcs arcCount=" + std::to_string(arcEdges.size()) + " created=" + std::to_string(createdCount));
				return createdCount;
			}
		}

		return CreateDimensionsForSource(sourceObject, plane);
	}

	static size_t CreateContinuousDimensions(const std::vector<MCObjectHandle>& sources, const ViewPlane::SViewPlane& plane)
	{
		const SAutoDimSettings s = GetAutoDimSettings();
		std::vector<ComplexGeometry::SMeasuredSegment> segments;
		for (MCObjectHandle source : sources) {
			SLineMeasurement line;
			if (GetLineMeasurement(source, line)) {
				segments.push_back({line.start, line.end, line.length});
				continue;
			}
			ComplexGeometry::SCollection geometry = ComplexGeometry::Collect(source);
			segments.insert(segments.end(), geometry.detailSegments.begin(), geometry.detailSegments.end());
		}
		std::sort(segments.begin(), segments.end(), [](const auto& a, const auto& b) {
			return std::min(a.start.x, a.end.x) < std::min(b.start.x, b.end.x);
		});
		if (segments.empty()) return 0;

		size_t createdCount = 0;
		gSDK->SetUndoMethod(kUndoSwapObjects);
		for (size_t index = 0; index < segments.size(); ++index) {
			const auto& segment = segments[index];
			const double dx = segment.end.x - segment.start.x;
			const double dy = segment.end.y - segment.start.y;
			if (segment.length <= kGeometryTolerance) continue;
			AddLinearDimension(segment.start, segment.end, -std::max<WorldCoord>(s.dimOffsetBase, segment.length * s.dimOffsetRatio), Vector2(dx / segment.length, dy / segment.length), kLinearDimensionTypeAligned, "continuous", plane, createdCount);
		}
		if (createdCount > 0) gSDK->EndUndoEvent();
		return createdCount;
	}

	static size_t CreateVirtualLineIntersectionDimensions(const WorldPt& firstPoint, const WorldPt& secondPoint, const std::vector<MCObjectHandle>& selectedSources, const ViewPlane::SViewPlane& plane)
	{
		const SAutoDimSettings s = GetAutoDimSettings();
		SLineMeasurement virtualLine;
		if (!BuildLineMeasurement(firstPoint, secondPoint, virtualLine)) {
			return 0;
		}

		std::vector<MCObjectHandle> candidates;
		if (!selectedSources.empty()) {
			candidates = selectedSources;
		}
		else {
			const WorldCoord minX = std::min(firstPoint.x, secondPoint.x);
			const WorldCoord minY = std::min(firstPoint.y, secondPoint.y);
			const WorldCoord maxX = std::max(firstPoint.x, secondPoint.x);
			const WorldCoord maxY = std::max(firstPoint.y, secondPoint.y);
			gSDK->ForEachObjectN(allVisible, [&](MCObjectHandle object) {
				if (!IsSupportedSource(object)) return;
				WorldCube cube;
				gSDK->GetObjectCube(object, cube);
				if (cube.MaxX() < minX || cube.MinX() > maxX || cube.MaxY() < minY || cube.MinY() > maxY) return;
				candidates.push_back(object);
			});
		}

		std::vector<WorldPt> intersections;
		for (MCObjectHandle source : candidates) {
			ComplexGeometry::SCollection geometry = ComplexGeometry::Collect(source);
			for (const ComplexGeometry::SMeasuredSegment& segment : geometry.segments) {
				const double segmentDx = segment.end.x - segment.start.x;
				const double segmentDy = segment.end.y - segment.start.y;
				const double det = segmentDx * virtualLine.dy - virtualLine.dx * segmentDy;
				if (std::abs(det) <= kGeometryTolerance) continue;
				const double qx = segment.start.x - firstPoint.x;
				const double qy = segment.start.y - firstPoint.y;
				const double t = (segmentDx * qy - qx * segmentDy) / det;
				const double u = (virtualLine.dx * qy - qx * virtualLine.dy) / det;
				if (t < -kGeometryTolerance || t > 1.0 + kGeometryTolerance || u < -kGeometryTolerance || u > 1.0 + kGeometryTolerance) continue;
				const WorldPt intersection(firstPoint.x + t * virtualLine.dx, firstPoint.y + t * virtualLine.dy);
				const double dedupTolerance = std::max(s.intersectionDedupTolerance, virtualLine.length * 1e-6);
				bool duplicate = false;
				for (const WorldPt& existing : intersections) {
					if (std::hypot(existing.x - intersection.x, existing.y - intersection.y) <= dedupTolerance) { duplicate = true; break; }
				}
				if (duplicate) continue;
				intersections.push_back(intersection);
			}
		}

		if (intersections.size() < 2) {
			VWAD_RUNTIME_TRACE("virtual-line-intersection insufficient intersections=" + std::to_string(intersections.size()));
			return 0;
		}

		std::sort(intersections.begin(), intersections.end(), [&](const WorldPt& a, const WorldPt& b) {
			const WorldCoord projectionA = (a.x - firstPoint.x) * virtualLine.dx + (a.y - firstPoint.y) * virtualLine.dy;
			const WorldCoord projectionB = (b.x - firstPoint.x) * virtualLine.dx + (b.y - firstPoint.y) * virtualLine.dy;
			return projectionA < projectionB;
		});

		const WorldCoord offset = -std::max<WorldCoord>(s.dimOffsetBase, virtualLine.length * s.dimOffsetRatio);
		const Vector2 direction(virtualLine.dx / virtualLine.length, virtualLine.dy / virtualLine.length);
		size_t createdCount = 0;
		gSDK->SetUndoMethod(kUndoSwapObjects);
		for (size_t index = 1; index < intersections.size(); ++index) {
			AddLinearDimension(intersections[index - 1], intersections[index], offset, direction, kLinearDimensionTypeAligned, "virtual-line-intersection", plane, createdCount);
		}
		if (createdCount > 0) gSDK->EndUndoEvent();

		VWAD_RUNTIME_TRACE("virtual-line-intersection candidates=" + std::to_string(candidates.size())
			+ " intersections=" + std::to_string(intersections.size())
			+ " created=" + std::to_string(createdCount)
			+ " plane=" + plane.name);
		return createdCount;
	}

	static size_t CreateBoundaryDimensionsForSelection(const std::vector<MCObjectHandle>& sources, const ViewPlane::SViewPlane& plane)
	{
		const SAutoDimSettings s = GetAutoDimSettings();
		if (sources.empty()) return 0;
		const WorldCube cube = GetSourcesCube(sources);
		ViewPlane::SPlanarBounds bounds;
		if (plane.planar) ViewPlane::AddCubeCorners(plane, cube, bounds);
		else { bounds.Add(WorldPt(cube.MinX(), cube.MinY())); bounds.Add(WorldPt(cube.MaxX(), cube.MaxY())); }
		if (!bounds.valid) return 0;
		const WorldCoord extent = std::max<WorldCoord>(bounds.Width(), bounds.Height());
		const WorldCoord offset = std::max<WorldCoord>(s.dimOffsetBase, extent * s.dimOffsetRatio);
		size_t createdCount = 0;
		gSDK->SetUndoMethod(kUndoSwapObjects);
		if (bounds.Width() > kGeometryTolerance) AddLinearDimension(WorldPt(bounds.minU, bounds.minV), WorldPt(bounds.maxU, bounds.minV), -offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "selection-boundary-width", plane, createdCount);
		if (bounds.Height() > kGeometryTolerance) AddLinearDimension(WorldPt(bounds.maxU, bounds.minV), WorldPt(bounds.maxU, bounds.maxV), offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "selection-boundary-height", plane, createdCount);
		if (createdCount > 0) gSDK->EndUndoEvent();
		return createdCount;
	}

	// Collects axis-aligned world bounding boxes of the selected source objects so the
	// text-avoid pass can treat them as read-only obstacles. Falls back to the object top-
	// plan bounds when geometry collection is empty or truncated (same fallback as
	// ComplexGeometry::CollectPlanBounds).
	static std::vector<WorldRect> BuildAvoidSourceObstacles(const std::vector<MCObjectHandle>& sources)
	{
		std::vector<WorldRect> sourceBounds;
		const size_t limit = std::min(sources.size(), ComplexGeometry::kMaximumObjects);
		if (sources.size() > ComplexGeometry::kMaximumObjects) {
			VWAD_RUNTIME_TRACE("edit-avoid obstacles truncated sources=" + std::to_string(sources.size())
				+ " limit=" + std::to_string(ComplexGeometry::kMaximumObjects));
		}
		for (size_t index = 0; index < limit; ++index) {
			ComplexGeometry::SCollection geometry = ComplexGeometry::Collect(sources[index]);
			bool haveBounds = false;
			if (!geometry.truncated) {
				ComplexGeometry::SAxisAlignedBounds aligned;
				if (ComplexGeometry::CalculateAxisAlignedBounds(geometry, aligned)) {
					sourceBounds.push_back(WorldRect(aligned.minX, aligned.maxY, aligned.maxX, aligned.minY));
					haveBounds = true;
				}
			}
			if (!haveBounds) {
				WorldRectVerts verts;
				if (gSDK->GetObjectTopPlanBounds(sources[index], verts) && !verts.IsEmpty()) {
					const WorldCoord minX = std::min(std::min(verts.topLeft.x, verts.topRight.x), std::min(verts.bottomLeft.x, verts.bottomRight.x));
					const WorldCoord maxX = std::max(std::max(verts.topLeft.x, verts.topRight.x), std::max(verts.bottomLeft.x, verts.bottomRight.x));
					const WorldCoord minY = std::min(std::min(verts.topLeft.y, verts.topRight.y), std::min(verts.bottomLeft.y, verts.bottomRight.y));
					const WorldCoord maxY = std::max(std::max(verts.topLeft.y, verts.topRight.y), std::max(verts.bottomLeft.y, verts.bottomRight.y));
					sourceBounds.push_back(WorldRect(minX, maxY, maxX, minY));
				}
			}
		}
		return sourceBounds;
	}

	static size_t AvoidDimensionTextCollisions(const std::vector<MCObjectHandle>& dimensions, const std::vector<WorldRect>& sourceBounds, std::vector<MCObjectHandle>& ioUndoRegistered)
	{
		const SAutoDimSettings s = GetAutoDimSettings();
		struct DimensionBoundsInfo {
			MCObjectHandle handle;
			WorldRect bounds;
			WorldPt start;
			WorldPt end;
			WorldCoord textOffset = 0.0;
			WorldCoord textAboveLine = 0.0;
			bool textInside = true;
			WorldCoord dimensionLength = 0.0;
		};

		std::vector<DimensionBoundsInfo> dimensionBounds;
		for (MCObjectHandle dimension : dimensions) {
			DimensionBoundsInfo info;
			info.handle = dimension;
			WorldPt start;
			WorldPt end;
			bool textInside = true;
			if (gSDK->GetObjectBounds(dimension, info.bounds) &&
				GetDimensionPoint(dimension, ovDimStartPt, start) &&
				GetDimensionPoint(dimension, ovDimEndPt, end) &&
				GetDimensionReal(dimension, ovDimTextOffsetInCurrUnits, info.textOffset) &&
				GetDimensionReal(dimension, ovDimTextAboveLineInCurrUnits, info.textAboveLine) &&
				GetDimensionBool(dimension, ovDimTextPosInside, textInside)) {
				info.textInside = textInside;
				info.start = start;
				info.end = end;
				info.dimensionLength = std::hypot(end.x - start.x, end.y - start.y);
				if (info.dimensionLength <= kGeometryTolerance) continue;
				dimensionBounds.push_back(info);
			}
		}
		// A single dimension can still collide with the selected source objects (the
		// dimension-vs-source pass below), so only bail out when there is nothing to
		// collide against at all.
		if (dimensionBounds.empty()) return 0;

		std::vector<bool> adjusted(dimensionBounds.size(), false);
		std::vector<bool> extrapolated(dimensionBounds.size(), false);
		std::vector<bool> roundAdjusted(dimensionBounds.size(), false);
		std::vector<bool> unresolvedThisRound(dimensionBounds.size(), false);
		std::vector<bool> extrapolationCandidate(dimensionBounds.size(), false);
		std::vector<size_t> consecutiveUnresolved(dimensionBounds.size(), 0);
		std::vector<WorldCoord> centroidSumX(dimensionBounds.size(), 0.0);
		std::vector<WorldCoord> centroidSumY(dimensionBounds.size(), 0.0);
		std::vector<size_t> centroidCount(dimensionBounds.size(), 0);
		constexpr size_t kExtrapolateMaxSteps = 3;
		// ovDimTextAboveLineInCurrUnits and the outside-text form of
		// ovDimTextOffsetInCurrUnits are document-unit values, while the collision
		// solver works in world coordinates. Convert the configured world distance
		// before writing it; the inside-text form remains a ratio and is handled
		// separately below.
		const WorldCoord avoidStepInCurrentUnits = gSDK->CoordLengthToUnitsLengthN(s.avoidStep);
		const WorldCoord extrapolateInitialInCurrentUnits = 2 * avoidStepInCurrentUnits;
		const WorldCoord extrapolateStepInCurrentUnits = avoidStepInCurrentUnits;
		constexpr WorldCoord kTolerance = 2.0;

		const auto rectsOverlap = [&](const WorldRect& a, const WorldRect& b) -> bool {
			return !(a.right + kTolerance < b.left || b.right + kTolerance < a.left ||
				a.bottom - kTolerance > b.top || b.bottom - kTolerance > a.top);
		};
		const auto stillCollides = [&](size_t dimension) -> bool {
			for (const WorldRect& obstacle : sourceBounds) {
				if (rectsOverlap(dimensionBounds[dimension].bounds, obstacle)) return true;
			}
			for (size_t other = 0; other < dimensionBounds.size(); ++other) {
				if (other == dimension) continue;
				if (rectsOverlap(dimensionBounds[dimension].bounds, dimensionBounds[other].bounds)) return true;
			}
			return false;
		};

		size_t finalCollisions = 0;
		for (size_t iteration = 0; iteration < s.avoidIterations; ++iteration) {
			finalCollisions = 0;
			std::fill(roundAdjusted.begin(), roundAdjusted.end(), false);
			std::fill(unresolvedThisRound.begin(), unresolvedThisRound.end(), false);
			std::fill(extrapolationCandidate.begin(), extrapolationCandidate.end(), false);
			std::fill(centroidSumX.begin(), centroidSumX.end(), 0.0);
			std::fill(centroidSumY.begin(), centroidSumY.end(), 0.0);
			std::fill(centroidCount.begin(), centroidCount.end(), 0);

			// Type 2: dimension vs source obstacles (new, higher priority). When no source is
			// selected the obstacle list is empty and this scan does not run at all.
			if (!sourceBounds.empty()) {
				for (size_t dimension = 0; dimension < dimensionBounds.size(); ++dimension) {
					DimensionBoundsInfo& info = dimensionBounds[dimension];
					const WorldCoord ux = (info.end.x - info.start.x) / info.dimensionLength;
					const WorldCoord uy = (info.end.y - info.start.y) / info.dimensionLength;
					const WorldCoord nx = -uy;
					const WorldCoord ny = ux;
					bool movedAwayFromSource = false;
					for (size_t obstacle = 0; obstacle < sourceBounds.size(); ++obstacle) {
						const WorldRect& sourceBox = sourceBounds[obstacle];
						if (!rectsOverlap(info.bounds, sourceBox)) continue;
						++finalCollisions;
						const WorldCoord csX = (sourceBox.left + sourceBox.right) * 0.5;
						const WorldCoord csY = (sourceBox.top + sourceBox.bottom) * 0.5;
						const WorldCoord signedDistance = info.textInside
							? info.textOffset * info.dimensionLength
							: ((info.textOffset < 0.0) ? CurrentUnitsToWorld(-info.textOffset) : info.dimensionLength - CurrentUnitsToWorld(info.textOffset));
						const WorldCoord tX = info.start.x + ux * signedDistance;
						const WorldCoord tY = info.start.y + uy * signedDistance;
						const WorldCoord awayX = tX - csX;
						const WorldCoord awayY = tY - csY;
						const WorldCoord sourceProjectionN = awayX * nx + awayY * ny;
						bool changed = false;
						if (std::abs(sourceProjectionN) > kTolerance) {
							const WorldCoord normalSign = (sourceProjectionN > 0.0) ? 1.0 : -1.0;
							const WorldCoord newAbove = info.textAboveLine + normalSign * avoidStepInCurrentUnits;
							changed = ApplyDimensionVariableTransaction(info.handle, {
								{ ovDimTextAboveLineInCurrUnits, TVariableBlock(static_cast<Real64>(newAbove)) },
								{ ovDimTextPosCalculated, TVariableBlock(static_cast<Boolean>(false)) },
							}, ioUndoRegistered);
							if (changed) info.textAboveLine = newAbove;
						}
						if (!changed) {
							const WorldCoord sourceProjectionU = awayX * ux + awayY * uy;
							const WorldCoord offsetSign = (sourceProjectionU > 0.0) ? 1.0 : -1.0;
							const WorldCoord step = info.textInside
								? s.avoidStep / info.dimensionLength
								: avoidStepInCurrentUnits;
							const WorldCoord newOffset = info.textOffset + offsetSign * step;
							if (info.textInside && (newOffset < 0.0 || newOffset > 1.0)) {
								extrapolationCandidate[dimension] = true;
							}
							else {
								changed = ApplyDimensionVariableTransaction(info.handle, {
									{ ovDimTextOffsetInCurrUnits, TVariableBlock(static_cast<Real64>(newOffset)) },
									{ ovDimTextPosCalculated, TVariableBlock(static_cast<Boolean>(false)) },
								}, ioUndoRegistered);
								if (changed) info.textOffset = newOffset;
							}
						}
						if (changed) {
							gSDK->GetObjectBounds(info.handle, info.bounds);
							adjusted[dimension] = true;
							roundAdjusted[dimension] = true;
							movedAwayFromSource = true;
						}
						else {
							unresolvedThisRound[dimension] = true;
							centroidSumX[dimension] += csX;
							centroidSumY[dimension] += csY;
							++centroidCount[dimension];
						}
						if (movedAwayFromSource) break;
					}
				}
			}

			// Type 1: dimension vs dimension (existing logic and direction rules unchanged).
			for (size_t first = 0; first < dimensionBounds.size(); ++first) {
				if (roundAdjusted[first]) continue;
				for (size_t second = first + 1; second < dimensionBounds.size(); ++second) {
					const WorldRect& a = dimensionBounds[first].bounds;
					const WorldRect& b = dimensionBounds[second].bounds;
					const bool overlaps = rectsOverlap(a, b);
					if (!overlaps) continue;
					++finalCollisions;

					const WorldCoord dx = (b.left + b.right - a.left - a.right) * 0.5;
					const WorldCoord dy = (b.top + b.bottom - a.top - a.bottom) * 0.5;
					DimensionBoundsInfo& info = dimensionBounds[first];
					bool changed = false;
					if (std::abs(dy) > std::abs(dx)) {
						const WorldCoord newAboveInCurrentUnits = info.textAboveLine + avoidStepInCurrentUnits;
						changed = ApplyDimensionVariableTransaction(info.handle, {
							{ ovDimTextAboveLineInCurrUnits, TVariableBlock(static_cast<Real64>(newAboveInCurrentUnits)) },
							{ ovDimTextPosCalculated, TVariableBlock(static_cast<Boolean>(false)) },
						}, ioUndoRegistered);
						if (changed) info.textAboveLine = newAboveInCurrentUnits;
					}
					else {
						// Inside text uses a ratio along the dimension; outside text uses
						// a signed document-unit offset from the nearer endpoint.
						const WorldCoord step = info.textInside
							? s.avoidStep / info.dimensionLength
							: avoidStepInCurrentUnits;
						const WorldCoord direction = (dx > 0.0) ? -1.0 : 1.0;
						const WorldCoord newOffset = info.textOffset + direction * step;
						if (info.textInside && (newOffset < 0.0 || newOffset > 1.0)) {
							extrapolationCandidate[first] = true;
							unresolvedThisRound[first] = true;
							centroidSumX[first] += (a.left + a.right + b.left + b.right) * 0.25;
							centroidSumY[first] += (a.top + a.bottom + b.top + b.bottom) * 0.25;
							++centroidCount[first];
							continue;
						}
						changed = ApplyDimensionVariableTransaction(info.handle, {
							{ ovDimTextOffsetInCurrUnits, TVariableBlock(static_cast<Real64>(newOffset)) },
							{ ovDimTextPosCalculated, TVariableBlock(static_cast<Boolean>(false)) },
						}, ioUndoRegistered);
						if (changed) {
							info.textOffset = newOffset;
						}
					}
					if (changed) {
						gSDK->GetObjectBounds(info.handle, info.bounds);
						adjusted[first] = true;
					}
					else {
						unresolvedThisRound[first] = true;
						centroidSumX[first] += (a.left + a.right + b.left + b.right) * 0.25;
						centroidSumY[first] += (a.top + a.bottom + b.top + b.bottom) * 0.25;
						++centroidCount[first];
					}
				}
			}

			// B extrapolation: push text beyond the witness line along the dimension direction
			// once, away from the overlap centroid (each dimension extrapolates only once).
			for (size_t dimension = 0; dimension < dimensionBounds.size(); ++dimension) {
				if (extrapolated[dimension] || !unresolvedThisRound[dimension]) continue;
				const bool triggerInside = extrapolationCandidate[dimension];
				const bool triggerConsecutive = consecutiveUnresolved[dimension] >= 2;
				const bool triggerStuck = roundAdjusted[dimension] && unresolvedThisRound[dimension];
				if (!triggerInside && !triggerConsecutive && !triggerStuck) continue;

				DimensionBoundsInfo& info = dimensionBounds[dimension];
				const WorldCoord ux = (info.end.x - info.start.x) / info.dimensionLength;
				const WorldCoord uy = (info.end.y - info.start.y) / info.dimensionLength;
				WorldCoord centroidX = (info.bounds.left + info.bounds.right) * 0.5;
				WorldCoord centroidY = (info.bounds.top + info.bounds.bottom) * 0.5;
				if (centroidCount[dimension] > 0) {
					centroidX = centroidSumX[dimension] / static_cast<WorldCoord>(centroidCount[dimension]);
					centroidY = centroidSumY[dimension] / static_cast<WorldCoord>(centroidCount[dimension]);
				}
				const WorldCoord projection = (centroidX - info.start.x) * ux + (centroidY - info.start.y) * uy;
				const WorldCoord firstSideSign = (projection < info.dimensionLength * 0.5) ? -1.0 : 1.0;
				const WorldCoord sideSigns[2] = { firstSideSign, -firstSideSign };
				bool extrapolationApplied = false;
				for (size_t sideIndex = 0; sideIndex < 2 && !extrapolationApplied; ++sideIndex) {
						WorldCoord off = extrapolateInitialInCurrentUnits;
					for (size_t stepIndex = 0; stepIndex < kExtrapolateMaxSteps && !extrapolationApplied; ++stepIndex) {
						const WorldCoord newOffset = sideSigns[sideIndex] * off;
						const bool written = ApplyDimensionVariableTransaction(info.handle, {
							{ ovDimTextOffsetInCurrUnits, TVariableBlock(static_cast<Real64>(newOffset)) },
							{ ovDimTextPosCalculated, TVariableBlock(static_cast<Boolean>(false)) },
						}, ioUndoRegistered);
						if (!written) break;
						info.textOffset = newOffset;
						gSDK->GetObjectBounds(info.handle, info.bounds);
						if (!stillCollides(dimension)) {
							extrapolated[dimension] = true;
							adjusted[dimension] = true;
							unresolvedThisRound[dimension] = false;
							extrapolationApplied = true;
						}
						else {
							off += extrapolateStepInCurrentUnits;
						}
					}
				}
				if (!extrapolationApplied) ++finalCollisions;
			}

			for (size_t dimension = 0; dimension < dimensionBounds.size(); ++dimension) {
				if (unresolvedThisRound[dimension]) ++consecutiveUnresolved[dimension];
				else consecutiveUnresolved[dimension] = 0;
			}
			VWAD_RUNTIME_TRACE("edit-avoid iteration=" + std::to_string(iteration)
				+ " collisions=" + std::to_string(finalCollisions)
				+ " dimensions=" + std::to_string(dimensionBounds.size()));
			if (finalCollisions == 0) break;
		}

		const size_t adjustedCount = static_cast<size_t>(std::count(adjusted.begin(), adjusted.end(), true));
		const size_t extrapolatedCount = static_cast<size_t>(std::count(extrapolated.begin(), extrapolated.end(), true));
		size_t unresolvedRemaining = 0;
		for (size_t dimension = 0; dimension < dimensionBounds.size(); ++dimension) {
			if (stillCollides(dimension)) ++unresolvedRemaining;
		}
		VWAD_RUNTIME_TRACE("edit-avoid completed finalCollisions=" + std::to_string(finalCollisions)
			+ " adjusted=" + std::to_string(adjustedCount)
			+ " extrapolated=" + std::to_string(extrapolatedCount)
			+ " unresolved=" + std::to_string(unresolvedRemaining));
		return adjustedCount;
	}

	struct SMergeDimensionInfo
	{
		MCObjectHandle handle = nullptr;
		WorldPt start;
		WorldPt end;
		Vector2 direction;
		WorldCoord minAxis = 0.0;
		WorldCoord maxAxis = 0.0;
		WorldCoord startOffset = 0.0;
		Uint8 dimensionClass = 0;
		InternalIndex objectClass = 0;
		TXString standardName;
	};

	static bool BuildMergeDimensionInfo(MCObjectHandle dimension, SMergeDimensionInfo& outInfo)
	{
		if (!dimension || gSDK->GetObjectTypeN(dimension) != dimHeaderNode) return false;
		if (!GetDimensionPoint(dimension, ovDimStartPt, outInfo.start) || !GetDimensionPoint(dimension, ovDimEndPt, outInfo.end)) return false;
		if (!GetDimensionUnsignedChar(dimension, ovDimClass, outInfo.dimensionClass) || outInfo.dimensionClass > 1) return false;
		if (!GetDimensionReal(dimension, ovDimStartOffset, outInfo.startOffset) || !GetDimensionString(dimension, ovDimStandardName, outInfo.standardName)) return false;

		const WorldCoord dx = outInfo.end.x - outInfo.start.x;
		const WorldCoord dy = outInfo.end.y - outInfo.start.y;
		const WorldCoord length = std::hypot(dx, dy);
		if (length <= kGeometryTolerance) return false;

		outInfo.handle = dimension;
		outInfo.direction = Vector2(dx / length, dy / length);
		// Canonicalize the direction so ordering does not depend on endpoint order.
		if (outInfo.direction.x < -kGeometryTolerance ||
			(std::abs(outInfo.direction.x) <= kGeometryTolerance && outInfo.direction.y < 0.0)) {
			outInfo.direction.x = -outInfo.direction.x;
			outInfo.direction.y = -outInfo.direction.y;
		}
		outInfo.minAxis = std::min(outInfo.start.x * outInfo.direction.x + outInfo.start.y * outInfo.direction.y,
			outInfo.end.x * outInfo.direction.x + outInfo.end.y * outInfo.direction.y);
		outInfo.maxAxis = std::max(outInfo.start.x * outInfo.direction.x + outInfo.start.y * outInfo.direction.y,
			outInfo.end.x * outInfo.direction.x + outInfo.end.y * outInfo.direction.y);
		outInfo.objectClass = gSDK->GetObjectClass(dimension);
		return true;
	}

	static bool HaveMatchingMergePresentation(MCObjectHandle first, MCObjectHandle second)
	{
		const std::array<short, 4> realSelectors = {
			ovDimTextAboveLineInCurrUnits,
			ovDimTextOffsetInCurrUnits,
			ovDimTextSizeInPoints,
			ovDimLeaderToLeft,
		};
		for (short selector : realSelectors) {
			double firstValue = 0.0;
			double secondValue = 0.0;
			if (!GetDimensionReal(first, selector, firstValue) || !GetDimensionReal(second, selector, secondValue) ||
				std::abs(firstValue - secondValue) > kGeometryTolerance) return false;
		}
		const std::array<short, 1> shortSelectors = { ovDimTextRotation };
		for (short selector : shortSelectors) {
			Sint16 firstValue = 0;
			Sint16 secondValue = 0;
			if (!GetDimensionShort(first, selector, firstValue) || !GetDimensionShort(second, selector, secondValue) || firstValue != secondValue) return false;
		}
		const std::array<short, 2> boolSelectors = { ovDimTextPosCalculated, ovDimTextPosInside };
		for (short selector : boolSelectors) {
			bool firstValue = false;
			bool secondValue = false;
			if (!GetDimensionBool(first, selector, firstValue) || !GetDimensionBool(second, selector, secondValue) || firstValue != secondValue) return false;
		}
		const std::array<short, 5> textSelectors = {
			ovDimLeaderText,
			ovDimTrailerText,
			ovDimLeaderText2,
			ovDimTrailerText2,
			ovDimNoteText,
		};
		for (short selector : textSelectors) {
			TXString firstValue;
			TXString secondValue;
			if (!GetDimensionString(first, selector, firstValue) || !GetDimensionString(second, selector, secondValue) || firstValue != secondValue) return false;
		}
		return true;
	}

	static bool ValidateAndSortMergeDimensions(const std::vector<MCObjectHandle>& dimensions, std::vector<SMergeDimensionInfo>& outDimensions)
	{
		if (dimensions.size() < 2) return false;
		outDimensions.clear();
		outDimensions.reserve(dimensions.size());
		for (MCObjectHandle dimension : dimensions) {
			SMergeDimensionInfo info;
			if (!BuildMergeDimensionInfo(dimension, info)) return false;
			outDimensions.push_back(info);
		}

		const SMergeDimensionInfo& reference = outDimensions.front();
		for (const SMergeDimensionInfo& candidate : outDimensions) {
			const WorldCoord cross = reference.direction.x * candidate.direction.y - reference.direction.y * candidate.direction.x;
			const WorldCoord relativeX = candidate.start.x - reference.start.x;
			const WorldCoord relativeY = candidate.start.y - reference.start.y;
			const WorldCoord lineDistance = std::abs(relativeX * reference.direction.y - relativeY * reference.direction.x);
			const WorldCoord tolerance = std::max<WorldCoord>(1e-4, std::max(reference.maxAxis - reference.minAxis, candidate.maxAxis - candidate.minAxis) * 1e-6);
			if (candidate.dimensionClass != reference.dimensionClass || candidate.objectClass != reference.objectClass ||
				candidate.standardName != reference.standardName || std::abs(candidate.startOffset - reference.startOffset) > tolerance ||
				std::abs(cross) > 1e-4 || lineDistance > tolerance ||
				!HaveMatchingMergePresentation(reference.handle, candidate.handle)) return false;
		}

		std::sort(outDimensions.begin(), outDimensions.end(), [](const SMergeDimensionInfo& first, const SMergeDimensionInfo& second) {
			if (first.minAxis != second.minAxis) return first.minAxis < second.minAxis;
			if (first.maxAxis != second.maxAxis) return first.maxAxis < second.maxAxis;
			if (first.start.x != second.start.x) return first.start.x < second.start.x;
			return first.start.y < second.start.y;
		});
		for (size_t index = 1; index < outDimensions.size(); ++index) {
			const WorldCoord overlapTolerance = std::max<WorldCoord>(1e-4, (outDimensions[index - 1].maxAxis - outDimensions[index - 1].minAxis) * 1e-6);
			if (outDimensions[index].minAxis < outDimensions[index - 1].maxAxis - overlapTolerance) return false;
		}
		return true;
	}

	static std::vector<MCObjectHandle> CollectSelectedSources();

	static size_t EditSelectedDimensions(size_t editMode, const ViewPlane::SViewPlane& plane, const std::vector<MCObjectHandle>& dimensions)
	{
		if (dimensions.empty()) return 0;

		gSDK->SetUndoMethod(kUndoSwapObjects);
		size_t changedCount = 0;
		std::vector<MCObjectHandle> undoRegistered;
		if (editMode == kEditAvoid) {
			const std::vector<MCObjectHandle> sources = CollectSelectedSources();
			const std::vector<WorldRect> sourceBounds = BuildAvoidSourceObstacles(sources);
			changedCount = AvoidDimensionTextCollisions(dimensions, sourceBounds, undoRegistered);
			if (changedCount > 0) gSDK->EndUndoEvent();
			VWAD_RUNTIME_TRACE("dimension-edit mode=" + std::to_string(editMode) + " selected=" + std::to_string(dimensions.size()) + " changed=" + std::to_string(changedCount));
			return changedCount;
		}
		const std::vector<MCObjectHandle> trimSources = editMode == kEditTrim ? CollectSelectedSources() : std::vector<MCObjectHandle>();
		std::vector<ComplexGeometry::SMeasuredSegment> trimGeometrySegments;
		if (editMode == kEditTrim) {
			for (MCObjectHandle source : trimSources) {
				ComplexGeometry::SCollection geometry = ComplexGeometry::Collect(source);
				trimGeometrySegments.insert(trimGeometrySegments.end(), geometry.segments.begin(), geometry.segments.end());
			}
		}
		std::vector<MCObjectHandle> chainSources;
		WorldCoord sharedOffset = 0.0;
		bool hasSharedOffset = false;
		if (editMode == kEditAlign && GetDimensionReal(dimensions.front(), ovDimStartOffset, sharedOffset)) hasSharedOffset = true;
		for (MCObjectHandle dimension : dimensions) {
			WorldPt start;
			WorldPt end;
			const bool hasPoints = GetDimensionPoint(dimension, ovDimStartPt, start) && GetDimensionPoint(dimension, ovDimEndPt, end);
			if (!hasPoints) continue;

			switch (editMode) {
			case kEditConvert:
			{
				Uint8 dimensionClass = 0;
				if (!GetDimensionUnsignedChar(dimension, ovDimClass, dimensionClass) || dimensionClass != 1) break;
				const double dx = end.x - start.x;
				const double dy = end.y - start.y;
				const double length = std::hypot(dx, dy);
				if (length <= kGeometryTolerance) break;
				WorldCoord sourceOffset = 0.0;
				GetDimensionReal(dimension, ovDimStartOffset, sourceOffset);
				const bool horizontal = std::abs(dx) >= std::abs(dy);
				const WorldPt projectionEnd = horizontal
					? WorldPt(end.x, start.y)
					: WorldPt(start.x, end.y);
				// The aligned offset is the perpendicular distance to the sloped axis, while
				// the orthogonal offset is the vertical (horizontal projection) or horizontal
				// (vertical projection) distance to the H/V axis. Scale by 1/cosθ (or 1/sinθ)
				// so the dimension line stays at the same world position; the factor is >= 1
				// and |dx|/|dy| > 0 is guaranteed by the length guard above.
				const WorldCoord scaleFactor = horizontal ? (length / std::abs(dx)) : (length / std::abs(dy));
				const WorldCoord convertedOffset = sourceOffset * scaleFactor;
				MCObjectHandle replacement = gSDK->CreateLinearDimension(start, projectionEnd, convertedOffset, 0.0, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho);
				if (replacement) {
					VWAD_RUNTIME_TRACE("edit-convert dimension=" + DescribeObject(dimension)
						+ " sourceOffset=" + std::to_string(sourceOffset)
						+ " convertedOffset=" + std::to_string(convertedOffset));
				}
				if (replacement) {
					ViewPlane::ApplyPlanarRef(replacement, plane);
					const Boolean replacementAdded = CopyDimensionPresentationFrom(dimension, replacement) && gSDK->AddAfterSwapObject(replacement);
					if (replacementAdded && gSDK->AddBeforeSwapObject(dimension)) {
						gSDK->DeleteObject(dimension, true);
						++changedCount;
					}
					else {
						gSDK->DeleteObject(replacement, replacementAdded);
					}
				}
				break;
			}
			case kEditTrim:
			{
				// Only explicitly selected source objects may affect trimming. The public
				// SDK does not expose a dependable source association for dimensions.
				if (trimGeometrySegments.empty()) break;
				const double dx = end.x - start.x;
				const double dy = end.y - start.y;
				const double length = std::hypot(dx, dy);
				if (length <= kGeometryTolerance) break;

				WorldCoord currentOffset = 0.0;
				GetDimensionReal(dimension, ovDimStartOffset, currentOffset);
				const WorldCoord normalX = -dy / length;
				const WorldCoord normalY = dx / length;
				const WorldCoord direction = currentOffset >= 0.0 ? 1.0 : -1.0;

				auto findNearestWitnessIntersection = [&](const WorldPt& endpoint, WorldCoord& outDistance) {
					bool found = false;
					WorldCoord nearest = std::numeric_limits<WorldCoord>::max();
					for (const auto& segment : trimGeometrySegments) {
						const WorldCoord sx = segment.end.x - segment.start.x;
						const WorldCoord sy = segment.end.y - segment.start.y;
						const WorldCoord det = normalX * sy - normalY * sx;
						if (std::abs(det) <= kGeometryTolerance) continue;
						const WorldCoord qx = segment.start.x - endpoint.x;
						const WorldCoord qy = segment.start.y - endpoint.y;
						const WorldCoord distance = (qx * sy - qy * sx) / det;
						const WorldCoord segmentRatio = (qx * normalY - qy * normalX) / det;
						if (segmentRatio < -kGeometryTolerance || segmentRatio > 1.0 + kGeometryTolerance ||
							distance * direction < -kGeometryTolerance) continue;
						if (!found || std::abs(distance) < std::abs(nearest)) {
							nearest = distance;
							found = true;
						}
					}
					if (found) outDistance = std::abs(nearest);
					return found;
				};

				WorldCoord startProjection = 0.0;
				WorldCoord endProjection = 0.0;
				if (!findNearestWitnessIntersection(start, startProjection) ||
					!findNearestWitnessIntersection(end, endProjection)) break;

				const double startOffsetPageInches = gSDK->CoordLengthToPageLengthN(startProjection);
				const double endOffsetPageInches = gSDK->CoordLengthToPageLengthN(endProjection);

				const bool changed = ApplyDimensionVariableTransaction(dimension, {
					{ ovDimWitnessOverride, TVariableBlock(static_cast<Sint16>(4)) },
					{ ovDimCustStartWitOffset, TVariableBlock(static_cast<Real64>(startOffsetPageInches)) },
					{ ovDimCustEndWitOffset, TVariableBlock(static_cast<Real64>(endOffsetPageInches)) },
				}, undoRegistered);
				if (changed) {
					++changedCount;
					VWAD_RUNTIME_TRACE("edit-trim dimension=" + DescribeObject(dimension)
						+ " sources=" + std::to_string(trimSources.size())
						+ " segments=" + std::to_string(trimGeometrySegments.size())
						+ " startOffset=" + std::to_string(startOffsetPageInches)
						+ " endOffset=" + std::to_string(endOffsetPageInches));
				}
				break;
			}
			case kEditAlign:
			{
				if (hasSharedOffset && ApplyDimensionVariableTransaction(dimension, {
					{ ovDimStartOffset, TVariableBlock(static_cast<Real64>(sharedOffset)) },
				}, undoRegistered)) ++changedCount;
				break;
			}
			case kEditSplitExtend:
			{
				Uint8 dimensionClass = 0;
				if (!GetDimensionUnsignedChar(dimension, ovDimClass, dimensionClass) || dimensionClass > 1) break;
				const double dx = end.x - start.x;
				const double dy = end.y - start.y;
				const double length = std::hypot(dx, dy);
				if (length > kGeometryTolerance) {
					const WorldPt midpoint((start.x + end.x) * 0.5, (start.y + end.y) * 0.5);
					const Vector2 direction(dx / length, dy / length);
					WorldCoord sourceOffset = 0.0;
					GetDimensionReal(dimension, ovDimStartOffset, sourceOffset);
					MCObjectHandle firstHalf = gSDK->CreateLinearDimension(start, midpoint, sourceOffset, 0.0, direction, kLinearDimensionTypeAligned);
					MCObjectHandle secondHalf = gSDK->CreateLinearDimension(midpoint, end, sourceOffset, 0.0, direction, kLinearDimensionTypeAligned);
					if (firstHalf && secondHalf) {
						ApplyDimensionPresentation(firstHalf, plane, "split-first");
						ApplyDimensionPresentation(secondHalf, plane, "split-second");
						const Boolean presentationCopied = CopyDimensionPresentationFrom(dimension, firstHalf) && CopyDimensionPresentationFrom(dimension, secondHalf);
						const Boolean firstAdded = presentationCopied && gSDK->AddAfterSwapObject(firstHalf);
						const Boolean secondAdded = presentationCopied && gSDK->AddAfterSwapObject(secondHalf);
						if (firstAdded && secondAdded && gSDK->AddBeforeSwapObject(dimension)) {
							gSDK->DeleteObject(dimension, true);
							changedCount += 2;
						}
						else {
							gSDK->DeleteObject(firstHalf, firstAdded);
							gSDK->DeleteObject(secondHalf, secondAdded);
						}
					}
					else {
						if (firstHalf) gSDK->DeleteObject(firstHalf, false);
						if (secondHalf) gSDK->DeleteObject(secondHalf, false);
					}
				}
				break;
			}
			case kEditTextDirection:
				if (ApplyDimensionVariableTransaction(dimension, {
					{ ovDimTextRotation, TVariableBlock(static_cast<Sint16>(kHorVert)) },
				}, undoRegistered)) ++changedCount;
				break;
			case kEditPoints:
				if (start.x > end.x || (std::abs(start.x - end.x) <= kGeometryTolerance && start.y > end.y)) {
					if (SwapDimensionEndpointsAndWitnessOffsets(dimension, start, end, undoRegistered)) ++changedCount;
				}
				break;
			case kEditMerge:
				chainSources.push_back(dimension);
				break;
			case kEditAvoid:
				break;
			case kEditResetText:
				if (ApplyDimensionVariableTransaction(dimension, {
					{ ovDimLeaderText, TVariableBlock(TXString()) },
					{ ovDimTrailerText, TVariableBlock(TXString()) },
					{ ovDimLeaderText2, TVariableBlock(TXString()) },
					{ ovDimTrailerText2, TVariableBlock(TXString()) },
					{ ovDimNoteText, TVariableBlock(TXString()) },
				}, undoRegistered)) ++changedCount;
				break;
			case kEditResetTextPosition:
				if (ApplyDimensionVariableTransaction(dimension, {
					{ ovDimTextPosCalculated, TVariableBlock(static_cast<Boolean>(true)) },
					{ ovDimTextRotation, TVariableBlock(static_cast<Sint16>(kAlign)) },
				}, undoRegistered)) ++changedCount;
				break;
			default:
				break;
			}
		}

		if (editMode == kEditMerge && chainSources.size() >= 2) {
			const size_t mergeChangedCount = changedCount;
			std::vector<MCObjectHandle> mergeChains;
			bool mergeFailed = false;
			std::vector<SMergeDimensionInfo> orderedChainSources;
			if (ValidateAndSortMergeDimensions(chainSources, orderedChainSources)) {
				// Build every chain segment first. The undo table is not touched during
				// building, so a failure here rolls back with plain deletes and leaves no
				// undo records behind.
				MCObjectHandle currentChain = nullptr;
				for (size_t index = 1; index < orderedChainSources.size(); ++index) {
					const MCObjectHandle left = currentChain ? currentChain : orderedChainSources.front().handle;
					MCObjectHandle chain = gSDK->CreateChainDimension(left, orderedChainSources[index].handle);
					if (!chain) {
						mergeFailed = true;
						break;
					}
					ViewPlane::ApplyPlanarRef(chain, plane);
					if (!CopyDimensionPresentationFrom(orderedChainSources.front().handle, chain)) {
						gSDK->DeleteObject(chain, false);
						mergeFailed = true;
						break;
					}
					mergeChains.push_back(chain);
					currentChain = chain;
				}
			}

			if (!mergeFailed && !mergeChains.empty()) {
				// Intermediate chains are scaffolding that feeds the next CreateChainDimension
				// call. They are never registered in the undo table, so a plain delete is
				// enough and Undo must not resurrect them (they never existed beforehand).
				for (size_t index = 0; index + 1 < mergeChains.size(); ++index) {
					gSDK->DeleteObject(mergeChains[index], false);
				}

				// Register the original dimensions for undo re-insertion before keeping the
				// final chain, so no failure can leave the final chain registered while the
				// merge is being rolled back.
				MCObjectHandle finalChain = mergeChains.back();
				for (const SMergeDimensionInfo& source : orderedChainSources) {
					if (!source.handle || gSDK->GetObjectTypeN(source.handle) != dimHeaderNode) continue;
					if (!gSDK->AddBeforeSwapObject(source.handle)) {
						mergeFailed = true;
						break;
					}
				}
				if (!mergeFailed && !gSDK->AddAfterSwapObject(finalChain)) {
					mergeFailed = true;
				}
				if (!mergeFailed) {
					for (const SMergeDimensionInfo& source : orderedChainSources) {
						if (source.handle && gSDK->GetObjectTypeN(source.handle) == dimHeaderNode) {
							gSDK->DeleteObject(source.handle, true);
						}
					}
					changedCount = mergeChangedCount + 1;
				}
			}
			if (mergeFailed) {
				// Cancelled merge: every chain is temporary scaffolding that was never kept
				// via AddAfterSwapObject, so a plain delete removes it without an undo record.
				for (MCObjectHandle chain : mergeChains) {
					if (chain && gSDK->GetObjectTypeN(chain) == dimHeaderNode) gSDK->DeleteObject(chain, false);
				}
				changedCount = mergeChangedCount;
			}
		}
		if (changedCount > 0) gSDK->EndUndoEvent();
		VWAD_RUNTIME_TRACE("dimension-edit mode=" + std::to_string(editMode) + " selected=" + std::to_string(dimensions.size()) + " changed=" + std::to_string(changedCount));
		return changedCount;
	}

	// Front, back, left and right views measure the real Z range of the source instead
	// of the top view projection. The dimensions are created in the coordinates of the
	// measuring plane, so the horizontal dimension is the extent the camera actually
	// sees and the vertical dimension is the true model height.
	static size_t CreateElevationDimensionsForSource(MCObjectHandle sourceObject, const ViewPlane::SViewPlane& plane)
	{
		const SAutoDimSettings s = GetAutoDimSettings();
		WorldCube objectBounds;
		gSDK->GetObjectCube(sourceObject, objectBounds);

		ViewPlane::SPlanarBounds cubeBounds;
		ViewPlane::AddCubeCorners(plane, objectBounds, cubeBounds);
		if (!cubeBounds.valid) {
			VWAD_RUNTIME_TRACE("dimension-tool elevation bounds failed " + DescribeObject(sourceObject));
			return 0;
		}

		// The object cube is the only source of a real Z range, but it is generous for
		// rotated symbols and lighting devices. The plane axes never carry a Z
		// component, so the traversed 2D geometry can tighten the horizontal range
		// without touching the measured height.
		double minU = cubeBounds.minU;
		double maxU = cubeBounds.maxU;
		const char* horizontalSource = "object-cube";
		ComplexGeometry::SCollection geometry = ComplexGeometry::Collect(sourceObject);
		ComplexGeometry::SAxisAlignedBounds geometryBounds;
		if (ComplexGeometry::CalculateAxisAlignedBounds(geometry, geometryBounds)) {
			ViewPlane::SPlanarBounds geometryPlanar;
			const double xs[2] = { geometryBounds.minX, geometryBounds.maxX };
			const double ys[2] = { geometryBounds.minY, geometryBounds.maxY };
			for (double x : xs) {
				for (double y : ys) {
					geometryPlanar.Add(ViewPlane::Project(plane, WorldPt3(x, y, objectBounds.MinZ())));
				}
			}
			if (geometryPlanar.valid &&
				geometryPlanar.Width() > kGeometryTolerance &&
				geometryPlanar.Width() <= cubeBounds.Width() + kGeometryTolerance) {
				minU = geometryPlanar.minU;
				maxU = geometryPlanar.maxU;
				horizontalSource = "2d-geometry";
			}
		}

		const WorldCoord width = maxU - minU;
		const WorldCoord height = cubeBounds.Height();
		const WorldCoord offset = std::max<WorldCoord>(s.dimOffsetBase, std::max(width, height) * s.dimOffsetRatio);

		size_t createdCount = 0;
		gSDK->SetUndoMethod(kUndoSwapObjects);
		if (width > kGeometryTolerance) {
			AddLinearDimension(
				WorldPt(minU, cubeBounds.minV),
				WorldPt(maxU, cubeBounds.minV),
				-offset,
				Vector2(0.0, 0.0),
				kLinearDimensionTypeOrtho,
				"view-width",
				plane,
				createdCount);
		}
		if (height > kGeometryTolerance) {
			AddLinearDimension(
				WorldPt(maxU, cubeBounds.minV),
				WorldPt(maxU, cubeBounds.maxV),
				offset,
				Vector2(0.0, 0.0),
				kLinearDimensionTypeOrtho,
				"view-depth",
				plane,
				createdCount);
		}
		if (createdCount > 0) {
			gSDK->EndUndoEvent();
		}

		#if defined(_DEBUG)
		std::ostringstream trace;
		trace.precision(12);
		trace << "dimension-tool elevation " << DescribeObject(sourceObject)
			<< " plane=" << plane.name
			<< " measuredWidth=" << width
			<< " measuredHeight=" << height
			<< " horizontalSource=" << horizontalSource
			<< " zMin=" << objectBounds.MinZ()
			<< " zMax=" << objectBounds.MaxZ()
			<< " offset=" << offset
			<< " created=" << createdCount;
		VWAD_RUNTIME_TRACE(trace.str());
		#endif
		return createdCount;
	}

	static size_t CreateDimensionsForSource(MCObjectHandle sourceObject, const ViewPlane::SViewPlane& plane)
	{
		const SAutoDimSettings s = GetAutoDimSettings();
		if (!IsSupportedSource(sourceObject)) {
			VWAD_RUNTIME_TRACE("dimension-tool rejected source");
			return 0;
		}

		if (plane.planar) {
			return CreateElevationDimensionsForSource(sourceObject, plane);
		}

		WorldCube objectBounds;
		gSDK->GetObjectCube(sourceObject, objectBounds);
		SLineMeasurement lineMeasurement;
		const bool hasLineMeasurement = GetLineMeasurement(sourceObject, lineMeasurement);
		ComplexGeometry::SCollection geometry;
		ComplexGeometry::SDominantAxis dominantAxis;
		ComplexGeometry::SAxisAlignedBounds geometryBounds;
		ComplexGeometry::SOrientedBounds orientedBounds;
		bool hasGeometryBounds = false;
		bool hasOrientedBounds = false;
		if (!hasLineMeasurement) {
			geometry = ComplexGeometry::Collect(sourceObject);
			ComplexGeometry::FindDominantAxis(geometry, dominantAxis);
			hasGeometryBounds = ComplexGeometry::CalculateAxisAlignedBounds(geometry, geometryBounds);
			if (!geometry.sourceIsOpenPath && dominantAxis.valid && dominantAxis.foldedAngleDegrees > 2.0) {
				hasOrientedBounds = ComplexGeometry::CalculateOrientedBounds(geometry, dominantAxis, orientedBounds);
			}

			#if defined(_DEBUG)
			std::ostringstream geometryTrace;
			geometryTrace.precision(12);
			geometryTrace << "dimension-tool geometry objects=" << geometry.visitedObjectCount
				<< " points=" << geometry.points.size()
				<< " segments=" << geometry.segments.size()
				<< " detailSegments=" << geometry.detailSegments.size()
				<< " openPath=" << (geometry.sourceIsOpenPath ? "true" : "false")
				<< " truncated=" << (geometry.truncated ? "true" : "false")
				<< " dominant=" << (dominantAxis.valid ? "true" : "false")
				<< " geometryBounds=" << (hasGeometryBounds ? "true" : "false");
			if (hasGeometryBounds) {
				geometryTrace << " geometryWidth=" << geometryBounds.width
					<< " geometryHeight=" << geometryBounds.height;
			}
			if (dominantAxis.valid) {
				geometryTrace << " direction=" << FormatPoint(dominantAxis.direction)
					<< " foldedAngleDegrees=" << dominantAxis.foldedAngleDegrees
					<< " supportLength=" << dominantAxis.supportingLength;
			}
			VWAD_RUNTIME_TRACE(geometryTrace.str());
			#endif
		}
		if (hasLineMeasurement) {
			#if defined(_DEBUG)
			std::ostringstream lineTrace;
			lineTrace.precision(12);
			lineTrace << "dimension-tool line start=" << FormatPoint(lineMeasurement.start)
				<< " end=" << FormatPoint(lineMeasurement.end)
				<< " dx=" << lineMeasurement.dx
				<< " dy=" << lineMeasurement.dy
				<< " length=" << lineMeasurement.length
				<< " angle-degrees=" << lineMeasurement.angleDegrees;
			VWAD_RUNTIME_TRACE(lineTrace.str());
			#endif
		}

		double minX = objectBounds.MinX();
		double minY = objectBounds.MinY();
		double maxX = objectBounds.MaxX();
		double maxY = objectBounds.MaxY();
		const char* boundsSource = "object-cube";
		if (!hasLineMeasurement && hasGeometryBounds) {
			minX = geometryBounds.minX;
			minY = geometryBounds.minY;
			maxX = geometryBounds.maxX;
			maxY = geometryBounds.maxY;
			boundsSource = "2d-geometry";
		}

		const WorldCoord width = maxX - minX;
		const WorldCoord height = maxY - minY;
		WorldCoord measuredExtent = std::max(width, height);
		if (hasOrientedBounds) {
			measuredExtent = std::max<WorldCoord>(measuredExtent, std::max(orientedBounds.width, orientedBounds.height));
		}
		const WorldCoord offset = std::max<WorldCoord>(s.dimOffsetBase, measuredExtent * s.dimOffsetRatio);
		const WorldPt leftBottom(minX, minY);
		const WorldPt rightBottom(maxX, minY);
		const WorldPt rightTop(maxX, maxY);
		const bool hasOpenDetails = geometry.sourceIsOpenPath && !geometry.detailSegments.empty();
		const bool shouldCreateOverall = hasLineMeasurement || (!hasOpenDetails && !hasOrientedBounds);

		size_t createdCount = 0;
		gSDK->SetUndoMethod(kUndoSwapObjects);
		if (shouldCreateOverall && width > kGeometryTolerance) {
			AddLinearDimension(leftBottom, rightBottom, -offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "horizontal", plane, createdCount);
		}
		if (shouldCreateOverall && height > kGeometryTolerance) {
			AddLinearDimension(rightBottom, rightTop, offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "vertical", plane, createdCount);
		}

		if (hasLineMeasurement && std::abs(lineMeasurement.dx) > kGeometryTolerance && std::abs(lineMeasurement.dy) > kGeometryTolerance) {
			const Vector2 lineDirection(
				lineMeasurement.dx / lineMeasurement.length,
				lineMeasurement.dy / lineMeasurement.length);
			AddLinearDimension(
				lineMeasurement.start,
				lineMeasurement.end,
				-offset * 0.75,
				lineDirection,
				kLinearDimensionTypeAligned,
				"aligned-length",
				plane,
				createdCount);

			WorldPt angleCenter;
			WorldPt angleP1;
			WorldPt angleP2;
			if (GetLineAngleDefinition(lineMeasurement, offset, angleCenter, angleP1, angleP2)) {
				AddAngleDimension(angleCenter, angleP1, angleP2, offset * 1.25, "angle", plane, createdCount);
			}
		}
		else if (!hasLineMeasurement && dominantAxis.valid) {
			if (geometry.sourceIsOpenPath) {
				std::vector<ComplexGeometry::SMeasuredSegment> details = geometry.detailSegments;
				std::sort(details.begin(), details.end(), [](const ComplexGeometry::SMeasuredSegment& a, const ComplexGeometry::SMeasuredSegment& b) {
					return a.length > b.length;
				});
				const size_t detailCount = std::min<size_t>(3, details.size());
				for (size_t index = 0; index < detailCount; ++index) {
					const ComplexGeometry::SMeasuredSegment& segment = details[index];
					const Vector2 direction(
						(segment.end.x - segment.start.x) / segment.length,
						(segment.end.y - segment.start.y) / segment.length);
					AddLinearDimension(
						segment.start,
						segment.end,
						-offset * (0.55 + static_cast<double>(index) * 0.35),
						direction,
						kLinearDimensionTypeAligned,
						"open-path-segment",
						plane,
						createdCount);
				}
			}
			else if (hasOrientedBounds) {
				#if defined(_DEBUG)
				std::ostringstream orientedTrace;
				orientedTrace.precision(12);
				orientedTrace << "dimension-tool oriented width=" << orientedBounds.width
					<< " height=" << orientedBounds.height;
				VWAD_RUNTIME_TRACE(orientedTrace.str());
				#endif

				if (orientedBounds.width > kGeometryTolerance) {
					AddLinearDimension(
						orientedBounds.widthStart,
						orientedBounds.widthEnd,
						-offset,
						Vector2(dominantAxis.direction.x, dominantAxis.direction.y),
						kLinearDimensionTypeAligned,
						"oriented-width",
						plane,
						createdCount);
				}
				if (orientedBounds.height > kGeometryTolerance) {
					AddLinearDimension(
						orientedBounds.heightStart,
						orientedBounds.heightEnd,
						offset,
						Vector2(-dominantAxis.direction.y, dominantAxis.direction.x),
						kLinearDimensionTypeAligned,
						"oriented-height",
						plane,
						createdCount);
				}
			}

			if (dominantAxis.foldedAngleDegrees > 2.0) {
				SLineMeasurement dominantMeasurement;
				if (BuildLineMeasurement(dominantAxis.referenceSegment.start, dominantAxis.referenceSegment.end, dominantMeasurement)) {
					WorldPt angleCenter;
					WorldPt angleP1;
					WorldPt angleP2;
					if (GetLineAngleDefinition(dominantMeasurement, offset, angleCenter, angleP1, angleP2)) {
						AddAngleDimension(angleCenter, angleP1, angleP2, offset * 1.25, "dominant-angle", plane, createdCount);
					}
				}
			}
		}
		if (createdCount > 0) {
			gSDK->EndUndoEvent();
		}

		#if defined(_DEBUG)
		std::ostringstream trace;
		trace.precision(12);
		trace << "dimension-tool source " << DescribeObject(sourceObject)
			<< " cubeWidth=" << (objectBounds.MaxX() - objectBounds.MinX())
			<< " cubeHeight=" << (objectBounds.MaxY() - objectBounds.MinY())
			<< " measuredWidth=" << width << " measuredHeight=" << height
			<< " boundsSource=" << boundsSource << " offset=" << offset
			<< " line=" << (hasLineMeasurement ? "true" : "false");
		if (hasLineMeasurement) {
			trace << " lineLength=" << lineMeasurement.length
				<< " lineAngleDegrees=" << lineMeasurement.angleDegrees;
		}
		trace
			<< " created=" << createdCount;
		VWAD_RUNTIME_TRACE(trace.str());
		#endif
		return createdCount;
	}

	struct SSelectionDimensionResult
	{
		size_t sourceCount = 0;
		size_t dimensionCount = 0;
	};

	static std::vector<MCObjectHandle> CollectSelectedSources()
	{
		std::vector<MCObjectHandle> selectedSources;
		VWFC::VWObjects::VWObjectIterator iterator(gSDK->FirstSelectedObject());
		while (iterator) {
			MCObjectHandle object = *iterator;
			if (IsSupportedSource(object)) {
				selectedSources.push_back(object);
			}
			iterator.MoveNextSelected();
		}
		return selectedSources;
	}

	static SSelectionDimensionResult CreateDimensionsForSelection(const std::vector<MCObjectHandle>& selectedSources, const ViewPlane::SViewPlane& plane)
	{
		SSelectionDimensionResult result;
		result.sourceCount = selectedSources.size();
		for (MCObjectHandle sourceObject : selectedSources) {
			result.dimensionCount += CreateDimensionsForSource(sourceObject, plane);
		}

		VWAD_RUNTIME_TRACE("selection-batch selected=" + std::to_string(gSDK->NumSelectedObjects())
			+ " sources=" + std::to_string(result.sourceCount)
			+ " plane=" + plane.name
			+ " dimensions=" + std::to_string(result.dimensionCount));
		return result;
	}

	// One measuring plane is shared by a whole batch so every dimension of one run stays
	// coplanar.
	static WorldCube GetSourcesCube(const std::vector<MCObjectHandle>& sources)
	{
		WorldCube totalCube;
		bool hasCube = false;
		for (MCObjectHandle sourceObject : sources) {
			WorldCube objectCube;
			gSDK->GetObjectCube(sourceObject, objectCube);
			if (!hasCube) {
				totalCube = objectCube;
				hasCube = true;
			}
			else {
				totalCube.Unite(objectCube);
			}
		}
		return totalCube;
	}

	static ViewPlane::SViewPlane BeginViewPlaneForSources(const std::vector<MCObjectHandle>& sources)
	{
		ViewPlane::SViewPlane plane = ViewPlane::Begin(GetSourcesCube(sources));
		VWAD_RUNTIME_TRACE(ViewPlane::Describe(plane));
		return plane;
	}

	static bool EndElevationPlaneWithAlert(ViewPlane::SViewPlane& plane, const char* alertText)
	{
		if (!plane.planar) return false;
		VWAD_RUNTIME_TRACE(std::string("dimension-tool rejected elevation mode plane=") + plane.name);
		ViewPlane::End(plane);
		gSDK->AlertInform(alertText);
		return true;
	}

	struct SSpacingSource
	{
		WorldPt center;
		WorldCoord extent = 0.0;
	};

	static bool GetSpacingSource(MCObjectHandle sourceObject, const ViewPlane::SViewPlane& plane, SSpacingSource& outSource)
	{
		if (plane.planar) {
			WorldCube objectBounds;
			gSDK->GetObjectCube(sourceObject, objectBounds);
			ViewPlane::SPlanarBounds planarBounds;
			ViewPlane::AddCubeCorners(plane, objectBounds, planarBounds);
			if (!planarBounds.valid ||
				(planarBounds.Width() <= kGeometryTolerance && planarBounds.Height() <= kGeometryTolerance)) {
				return false;
			}

			WorldPt3 center = ViewPlane::GetCubeCenter(objectBounds);
			const short planarObjectType = gSDK->GetObjectTypeN(sourceObject);
			if (planarObjectType == kSymbolNode || planarObjectType == kParametricNode) {
				TransformMatrix entityMatrix;
				gSDK->GetEntityMatrix(sourceObject, entityMatrix);
				center = WorldPt3(entityMatrix.P().x, entityMatrix.P().y, entityMatrix.P().z);
			}

			outSource.center = ViewPlane::Project(plane, center);
			outSource.extent = std::max<WorldCoord>(planarBounds.Width(), planarBounds.Height());
			return true;
		}

		ComplexGeometry::SCollection geometry = ComplexGeometry::Collect(sourceObject);
		ComplexGeometry::SAxisAlignedBounds bounds;
		if (ComplexGeometry::CalculateAxisAlignedBounds(geometry, bounds)) {
			outSource.center = WorldPt(
				(bounds.minX + bounds.maxX) * 0.5,
				(bounds.minY + bounds.maxY) * 0.5);
			outSource.extent = std::max<WorldCoord>(bounds.width, bounds.height);
		}
		else {
			WorldCube objectBounds;
			gSDK->GetObjectCube(sourceObject, objectBounds);
			const WorldCoord width = objectBounds.MaxX() - objectBounds.MinX();
			const WorldCoord height = objectBounds.MaxY() - objectBounds.MinY();
			if (width <= kGeometryTolerance && height <= kGeometryTolerance) {
				return false;
			}

			outSource.center = WorldPt(
				(objectBounds.MinX() + objectBounds.MaxX()) * 0.5,
				(objectBounds.MinY() + objectBounds.MaxY()) * 0.5);
			outSource.extent = std::max(width, height);
		}

		const short objectType = gSDK->GetObjectTypeN(sourceObject);
		if (objectType == kSymbolNode || objectType == kParametricNode) {
			TransformMatrix entityMatrix;
			gSDK->GetEntityMatrix(sourceObject, entityMatrix);
			outSource.center = WorldPt(entityMatrix.P().x, entityMatrix.P().y);
		}
		return true;
	}

	static size_t CreateSpacingDimensionsForSelection(const std::vector<MCObjectHandle>& selectedSources, const ViewPlane::SViewPlane& plane)
	{
		std::vector<SSpacingSource> spacingSources;
		for (MCObjectHandle sourceObject : selectedSources) {
			SSpacingSource spacingSource;
			if (GetSpacingSource(sourceObject, plane, spacingSource)) {
				spacingSources.push_back(spacingSource);
			}
		}
		if (spacingSources.size() < 2) {
			VWAD_RUNTIME_TRACE("spacing-batch requires at least two measurable sources");
			return 0;
		}

		const size_t sourceCount = spacingSources.size();
		const size_t noParent = std::numeric_limits<size_t>::max();
		std::vector<bool> connected(sourceCount, false);
		std::vector<double> nearestDistanceSquared(sourceCount, std::numeric_limits<double>::max());
		std::vector<size_t> nearestParent(sourceCount, noParent);
		nearestDistanceSquared[0] = 0.0;

		size_t createdCount = 0;
		gSDK->SetUndoMethod(kUndoSwapObjects);
		for (size_t connectedCount = 0; connectedCount < sourceCount; ++connectedCount) {
			size_t nextIndex = noParent;
			for (size_t index = 0; index < sourceCount; ++index) {
				if (!connected[index] && (nextIndex == noParent || nearestDistanceSquared[index] < nearestDistanceSquared[nextIndex])) {
					nextIndex = index;
				}
			}
			if (nextIndex == noParent) {
				break;
			}

			connected[nextIndex] = true;
			if (nearestParent[nextIndex] != noParent) {
				WorldPt start = spacingSources[nearestParent[nextIndex]].center;
				WorldPt end = spacingSources[nextIndex].center;
				WorldCoord dx = end.x - start.x;
				WorldCoord dy = end.y - start.y;
				const WorldCoord length = std::hypot(dx, dy);
				if (length > kGeometryTolerance) {
					if (dx < 0.0 || (std::abs(dx) <= kGeometryTolerance && dy < 0.0)) {
						std::swap(start, end);
						dx = -dx;
						dy = -dy;
					}
					const WorldCoord offset = std::max<WorldCoord>(
						50.0,
						std::max(spacingSources[nearestParent[nextIndex]].extent, spacingSources[nextIndex].extent) * 0.6);
					AddLinearDimension(
						start,
						end,
						-offset,
						Vector2(dx / length, dy / length),
						kLinearDimensionTypeAligned,
						"center-spacing",
						plane,
						createdCount);
				}
			}

			for (size_t candidate = 0; candidate < sourceCount; ++candidate) {
				if (connected[candidate]) {
					continue;
				}
				const WorldCoord dx = spacingSources[candidate].center.x - spacingSources[nextIndex].center.x;
				const WorldCoord dy = spacingSources[candidate].center.y - spacingSources[nextIndex].center.y;
				const double distanceSquared = dx * dx + dy * dy;
				if (distanceSquared < nearestDistanceSquared[candidate]) {
					nearestDistanceSquared[candidate] = distanceSquared;
					nearestParent[candidate] = nextIndex;
				}
			}
		}

		if (createdCount > 0) {
			gSDK->EndUndoEvent();
		}
		VWAD_RUNTIME_TRACE("spacing-batch sources=" + std::to_string(spacingSources.size())
			+ " plane=" + plane.name
			+ " dimensions=" + std::to_string(createdCount));
		return createdCount;
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
	VWAD_RUNTIME_TRACE("recalculate proxy " + DescribeObject(fhObject));
	if (!sourceObject || sourceObject == definitionObject) {
		VWAD_RUNTIME_TRACE("recalculate source resolution failed");
		return kObjectEventNoErr;
	}
	VWAD_RUNTIME_TRACE("recalculate source " + DescribeObject(sourceObject));

	MCObjectHandle duplicate = gSDK->DuplicateObject(sourceObject);
	if (duplicate) {
		VWAD_RUNTIME_TRACE("recalculate duplicate created " + DescribeObject(duplicate));
		VWParametricObj parametric(definitionObject);
		VWTransformMatrix objectToWorld;
		parametric.GetObjectToWorldTransform(objectToWorld);
		gSDK->TransformObject(duplicate, objectToWorld.GetInverted());
		const bool addedToProxy = gSDK->AddObjectToContainer(duplicate, fhObject);
		VWAD_RUNTIME_TRACE(std::string("recalculate add-to-proxy=") + (addedToProxy ? "true " : "false ") + DescribeObject(duplicate));
		if (!addedToProxy) {
			gSDK->DeleteObject(duplicate);
		}
	}
	else {
		VWAD_RUNTIME_TRACE("recalculate duplicate failed");
	}

	return kObjectEventNoErr;
}

bool CAutoDimensionObj_EventSink::OnAutoDimMessage_GetDisplayCategoryName(TXString& outstrCategory_UniversalName, TXString& outstrCategory_LocalizedNameName)
{
	outstrCategory_UniversalName = kCategoryUniversalName;
	outstrCategory_LocalizedNameName = TXResStr("KeeplAutoDim", "auto_dim_category");
	return true;
}

bool CAutoDimensionObj_EventSink::OnAutoDimMessage_GetLocalizedTypeName(const TXString& instrDimID_UniversalName, TXString& outstrDimID_LocalizedName)
{
	if (instrDimID_UniversalName == kOverallWidth) {
		outstrDimID_LocalizedName = TXResStr("KeeplAutoDim", "auto_dim_type_width");
		return true;
	}

	if (instrDimID_UniversalName == kOverallHeight) {
		outstrDimID_LocalizedName = TXResStr("KeeplAutoDim", "auto_dim_type_height");
		return true;
	}

	if (instrDimID_UniversalName == kOverallDepth) {
		outstrDimID_LocalizedName = TXResStr("KeeplAutoDim", "auto_dim_type_depth");
		return true;
	}

	return false;
}

bool CAutoDimensionObj_EventSink::OnAutoDimMessage_GetSupportedTypes(EViewTypes inView, std::vector<VectorWorks::Extension::TAutoDimensionTypeInfo>& outvecTypes)
{
	outvecTypes.clear();

	SSourceBounds bounds;
	if (!GetSourceBounds(fhObject, bounds)) {
		VWAD_RUNTIME_TRACE("supported-types bounds failed view=" + std::to_string(static_cast<int>(inView)));
		return false;
	}

	double width = 0.0;
	double height = 0.0;
	double depth = 0.0;
	GetViewLengths(inView, bounds, width, height, depth);
	constexpr double kMinimumDimension = 1e-6;

	if (width > kMinimumDimension) {
		AddSupportedType(outvecTypes, kOverallWidth, TXResStr("KeeplAutoDim", "auto_dim_type_width"), VectorWorks::Extension::EAutoDimensionPlacement::eHorizontalBottom);
	}
	if (height > kMinimumDimension) {
		AddSupportedType(outvecTypes, kOverallHeight, TXResStr("KeeplAutoDim", "auto_dim_type_height"), VectorWorks::Extension::EAutoDimensionPlacement::eVerticalRight);
	}
	if (depth > kMinimumDimension) {
		AddSupportedType(outvecTypes, kOverallDepth, TXResStr("KeeplAutoDim", "auto_dim_type_depth"), VectorWorks::Extension::EAutoDimensionPlacement::eHorizontalTop);
	}

	#if defined(_DEBUG)
	std::ostringstream trace;
	trace.precision(12);
	trace << "supported-types view=" << static_cast<int>(inView)
		<< " width=" << width << " height=" << height << " depth=" << depth
		<< " count=" << outvecTypes.size();
	VWAD_RUNTIME_TRACE(trace.str());
	#endif

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
		VWAD_RUNTIME_TRACE(std::string("dimension-definition failed id=") + GetDimensionTraceName(instrDimID_UniversalName)
			+ " view=" + std::to_string(static_cast<int>(inView)));
		return false;
	}

	outvecDimensions.emplace_back(start, end, 0);
	VWAD_RUNTIME_TRACE(std::string("dimension-definition id=") + GetDimensionTraceName(instrDimID_UniversalName)
		+ " view=" + std::to_string(static_cast<int>(inView))
		+ " placement=" + std::to_string(static_cast<int>(inPlace))
		+ " start=" + FormatPoint(start) + " end=" + FormatPoint(end));
	return true;
}

bool CAutoDimensionObjDefTool_EventSink::DoSetUp(bool bRestore, const IToolModeBarInitProvider* pModeBarInitProvider)
{
	const bool result = VWTool_EventSink::DoSetUp(bRestore, pModeBarInitProvider);
	fEditDimensions = CollectSelectedDimensions();
	TXStringArray annotationImages;
	const std::array<const char*, 11> annotationIconNames = {
		"ModeAuto.png", "ModeContinuous.png", "ModeLine.png", "ModeManualBlock.png", "ModeIntersection.png",
		"ModeSelection.png", "ModeCenters.png", "ModeBoundaries.png", "ModeClosedSpace.png", "ModeEnhanced.png",
		"ModeQuickChain.png"
	};
	for (const char* iconName : annotationIconNames) {
		TXString iconPath = "KeeplAutoDimTest/Images/";
		iconPath += iconName;
		annotationImages.Append(iconPath);
	}
	// The first argument is the initially selected button, not the group ID. Group IDs
	// come from registration order (annotation=0, edit=1, settings=2).
	pModeBarInitProvider->AddRadioModeGroup(fAnnotationMode, annotationImages);

	TXStringArray editImages;
	const std::array<const char*, 11> editIconNames = {
		"ModeEditNone.png", "ModeConvert.png", "ModeTrim.png", "ModeAlign.png", "ModeSplitExtend.png",
		"ModeTextDirection.png", "ModeDimensionPoints.png", "ModeMerge.png", "ModeAvoidText.png",
		"ModeResetText.png", "ModeResetTextPosition.png"
	};
	for (const char* iconName : editIconNames) {
		TXString iconPath = "KeeplAutoDimTest/Images/";
		iconPath += iconName;
		editImages.Append(iconPath);
	}
	pModeBarInitProvider->AddRadioModeGroup(fEditMode, editImages);
	pModeBarInitProvider->AddButtonModeGroup("Vectorworks/Images/ModeViewBar/Options.png");

	VectorWorks::TVWModeBarButtonHelpArray buttonHelp;
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("自动识别", "选中对象后自动创建水平和垂直标注。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("连续标注", "选中线或路径对象，连续创建标注。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("线对象标注", "标注线段端点、长度和方向。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("符号定位", "先点符号，再点标注放置位置，从插入点引出定位尺寸。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("交线标注", "点击两点，自动标注两点连线与图元的交点。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("选择对象标注", "为选中的对象分别创建标注。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("中心点标注", "选中多个对象，标注对象中心间距。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("边界标注", "选中多个符号，标注整体边界。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("封闭空间标注", "点击封闭多边形或封闭线进行标注。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("加强标注", "创建角度、半径、直径和弧长标注。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("快速连续标注", "点击首点后，逐点续接生成水平/垂直投影（转角）尺寸；ESC 结束。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("创建标注", "创建模式：退出编辑功能，开始创建标注。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("转换标注", "将选中的对齐标注转换为转角（水平/垂直投影）尺寸。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("尺寸线剪齐", "将选中标注的尺寸界线剪齐到对象边界。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("标注对齐", "将选中的标注对齐到同一条标注线。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("标注分割和延伸", "分割或延伸选中标注的标注点。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("修正文字方向", "修正选中标注的文字方向。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("修正标注点", "修正选中标注的标注点位置。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("合并标注", "将相邻的选中标注合并为连续标注。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("文字自动避让", "自动移动重叠的标注文字，避免相互遮挡。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("重置标注文字", "重置选中标注的文字内容。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("重置文字位置", "重置选中标注的文字位置。", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("设置…", "调整标注偏移、文字避让与交线去重参数。", VectorWorks::eModeBarButtonType_PrefButtonMode));
	gSDK->SetModeBarButtonsText(buttonHelp);
	VWAD_RUNTIME_TRACE("tool-setup annotation-mode=" + std::to_string(fAnnotationMode) + " edit-mode=" + std::to_string(fEditMode));
	return result;
}

void CAutoDimensionObjDefTool_EventSink::DoSetDown(bool bRestore, const IToolModeBarInitProvider* pModeBarInitProvider)
{
	VWTool_EventSink::DoSetDown(bRestore, pModeBarInitProvider);
	fChainActive = false;
	fChainCreatedCount = 0;
	fEditDimensions.clear();
}

void CAutoDimensionObjDefTool_EventSink::DoModeEvent(size_t modeGroupID, size_t newButtonID, size_t oldButtonID)
{
	if (modeGroupID == kSettingsButtonGroup) {
		OpenSettingsDialog();
		return;
	}
	(void)oldButtonID;
	if (modeGroupID == kAnnotationModeGroup) {
		fAnnotationMode = newButtonID;
		// Leaving edit mode silently would keep the edit group highlighted while
		// HandleComplete already routes to annotation, so mirror the reset in the bar.
		if (fEditMode != kEditNone && fpModeBarProvider) {
			fpModeBarProvider->SetModeGroupValue(kEditModeGroup, static_cast<Sint32>(kEditNone));
		}
		fEditMode = kEditNone;
		if (fAnnotationMode != kAnnotationQuickChain) {
			fChainActive = false;
			fChainCreatedCount = 0;
		}
		VWAD_RUNTIME_TRACE("tool-annotation-mode changed=" + std::to_string(fAnnotationMode));
	}
	else if (modeGroupID == kEditModeGroup) {
		fEditMode = newButtonID;
		if (fEditMode == kEditNone) fEditDimensions.clear();
		else {
			const std::vector<MCObjectHandle> selectedDimensions = CollectSelectedDimensions();
			if (!selectedDimensions.empty()) fEditDimensions = selectedDimensions;
		}
		VWAD_RUNTIME_TRACE("tool-edit-mode changed=" + std::to_string(fEditMode)
			+ " selected=" + std::to_string(gSDK->NumSelectedObjects())
			+ " dimensions-cached=" + std::to_string(fEditDimensions.size()));
	}
}

TToolStatus CAutoDimensionObjDefTool_EventSink::GetStatus(const IToolStatusProvider* pStatusProvider)
{
	if (fEditMode != kEditNone) {
		fResult = pStatusProvider->GetOnePointToolStatus();
	}
	else if (fAnnotationMode == kAnnotationManualBlock || fAnnotationMode == kAnnotationIntersection) {
		fResult = pStatusProvider->GetTwoPointToolStatus();
	}
	else if (fAnnotationMode == kAnnotationQuickChain) {
		fResult = pStatusProvider->GetOnePointToolStatus();
	}
	else {
		fResult = pStatusProvider->GetOnePointToolStatus();
	}
	this->Default();
	return fResult;
}

void CAutoDimensionObjDefTool_EventSink::HandleComplete()
{
	std::vector<MCObjectHandle> selectedDimensions = CollectSelectedDimensions();
	if (!selectedDimensions.empty()) {
		fEditDimensions = selectedDimensions;
	}
	else {
		selectedDimensions = fEditDimensions;
	}
	if (fEditMode != kEditNone) {
		if (selectedDimensions.empty()) {
			MCObjectHandle clickedObject = nullptr;
			short overPart = 0;
			SintptrT code = 0;
			::GS_TrackTool(gCBP, clickedObject, overPart, code);
			if (clickedObject && gSDK->GetObjectTypeN(clickedObject) == dimHeaderNode) {
				selectedDimensions.push_back(clickedObject);
				fEditDimensions = selectedDimensions;
			}
		}
		if (selectedDimensions.empty()) {
			gSDK->AlertInform("请先选择一个或多个尺寸标注对象，再使用编辑模式。");
			return;
		}
		VWAD_RUNTIME_TRACE("dimension-edit entry mode=" + std::to_string(fEditMode)
			+ " selected=" + std::to_string(gSDK->NumSelectedObjects())
			+ " dimensions=" + std::to_string(selectedDimensions.size()));
		if (fEditMode == kEditTrim && CollectSelectedSources().empty()) {
			gSDK->AlertInform("剪齐模式需要同时选中尺寸标注对象和源图形（线、多边形等），仅选尺寸不会产生剪齐效果。");
			fEditDimensions.clear();
			return;
		}
		ViewPlane::SViewPlane editPlane = BeginViewPlaneForSources(selectedDimensions);
		if (fEditMode == kEditTrim && EndElevationPlaneWithAlert(editPlane, "尺寸线剪齐仅支持平面/非立面视图，当前为标准立面视图，请切换到平面视图后重试。")) {
			return;
		}
		const size_t changedCount = EditSelectedDimensions(fEditMode, editPlane, selectedDimensions);
		ViewPlane::End(editPlane);
		if (changedCount == 0) gSDK->AlertInform("无法编辑所选的尺寸标注对象。");
		fEditDimensions.clear();
		return;
	}

	const std::vector<MCObjectHandle> selectedSources = CollectSelectedSources();
	if (fAnnotationMode == kAnnotationClosedSpace) {
		MCObjectHandle clickedSource = nullptr;
		short overPart = 0;
		SintptrT code = 0;
		::GS_TrackTool(gCBP, clickedSource, overPart, code);
		if (IsClosedSpaceSource(clickedSource)) {
			ViewPlane::SViewPlane clickPlane = BeginViewPlaneForSources({ clickedSource });
			const size_t count = CreateDimensionsForSource(clickedSource, clickPlane);
			ViewPlane::End(clickPlane);
			if (count == 0) gSDK->AlertInform("未找到可测量的水平或垂直范围。");
			return;
		}

		std::vector<MCObjectHandle> selectedClosedSpaces;
		for (MCObjectHandle source : selectedSources) {
			if (IsClosedSpaceSource(source)) selectedClosedSpaces.push_back(source);
		}
		if (selectedClosedSpaces.empty()) {
			gSDK->AlertInform("封闭空间模式请点击封闭多边形或封闭多段线。");
			return;
		}
		ViewPlane::SViewPlane selectionPlane = BeginViewPlaneForSources(selectedClosedSpaces);
		size_t count = 0;
		for (MCObjectHandle source : selectedClosedSpaces) count += CreateDimensionsForSource(source, selectionPlane);
		ViewPlane::End(selectionPlane);
		if (count == 0) gSDK->AlertInform("未找到可测量的水平或垂直范围。");
		return;
	}
	if (fAnnotationMode == kAnnotationContinuous) {
		if (selectedSources.empty()) {
			gSDK->AlertInform("请先选择线段或路径对象，再使用连续标注模式。");
			return;
		}
		ViewPlane::SViewPlane plane = BeginViewPlaneForSources(selectedSources);
		if (EndElevationPlaneWithAlert(plane, "连续标注模式仅支持平面/非立面视图，当前为标准立面视图，请切换到平面视图后重试。")) {
			return;
		}
		const size_t count = CreateContinuousDimensions(selectedSources, plane);
		ViewPlane::End(plane);
		if (count == 0) gSDK->AlertInform("未找到可测量的路径线段。");
		return;
	}
	if (fAnnotationMode == kAnnotationIntersection) {
		if (this->GetToolPointsCount() < 2) {
			gSDK->AlertInform("交线标注请依次点击两点，确定测量直线。");
			return;
		}
		const VWPoint2D firstPoint = this->GetToolPt2D(0);
		const VWPoint2D secondPoint = this->GetToolPt2D(1);
		const WorldPt first(firstPoint.x, firstPoint.y);
		const WorldPt second(secondPoint.x, secondPoint.y);

		const WorldCoord pad = 1.0;
		WorldCube segmentCube(
			WorldPt3(std::min(first.x, second.x) - pad, std::min(first.y, second.y) - pad, -pad),
			WorldPt3(std::max(first.x, second.x) + pad, std::max(first.y, second.y) + pad, pad));
		ViewPlane::SViewPlane plane = ViewPlane::Begin(segmentCube);
		if (EndElevationPlaneWithAlert(plane, "交线标注仅支持平面/非立面视图，当前为标准立面视图，请切换到平面视图后重试。")) {
			return;
		}
		const size_t count = CreateVirtualLineIntersectionDimensions(first, second, selectedSources, plane);
		ViewPlane::End(plane);
		if (count == 0) gSDK->AlertInform("两点连线上未找到与图元的交点。");
		return;
	}
	if (fAnnotationMode == kAnnotationQuickChain) {
		if (this->GetToolPointsCount() < 1) return;
		const VWPoint2D click = this->GetToolPt2D(0);
		if (!fChainActive) {
			fChainAnchor = click;
			fChainActive = true;
			fChainCreatedCount = 0;
			VWAD_RUNTIME_TRACE(std::string("quick-chain anchor=") + FormatPoint(WorldPt(click.x, click.y)));
			gSDK->AlertInform("快速连续标注：已锚定首点，点击下一点续接，ESC 结束。");
			return;
		}

		const WorldCoord dx = click.x - fChainAnchor.x;
		const WorldCoord dy = click.y - fChainAnchor.y;
		const WorldCoord len = std::hypot(dx, dy);
		if (len <= kGeometryTolerance) {
			gSDK->AlertInform("与上一锚点距离过近，未生成标注。");
			return;
		}

		const WorldCoord pad = 1.0;
		WorldCube segCube(
			WorldPt3(std::min(fChainAnchor.x, click.x) - pad, std::min(fChainAnchor.y, click.y) - pad, -pad),
			WorldPt3(std::max(fChainAnchor.x, click.x) + pad, std::max(fChainAnchor.y, click.y) + pad, pad));
		ViewPlane::SViewPlane plane = ViewPlane::Begin(segCube);
		if (EndElevationPlaneWithAlert(plane, "快速连续标注仅支持平面/非立面视图，当前为标准立面视图，请切换到平面视图后重试。")) {
			// Disarm the chain so a subsequent click restarts from a fresh anchor instead
			// of re-triggering the alert against the stale anchor.
			fChainActive = false;
			fChainCreatedCount = 0;
			return;
		}
		size_t created = 0;
		gSDK->SetUndoMethod(kUndoSwapObjects);
		const SAutoDimSettings s = GetAutoDimSettings();
		const WorldCoord offset = std::max<WorldCoord>(s.dimOffsetBase, len * s.dimOffsetRatio);
		if (std::abs(dx) >= std::abs(dy)) {
			AddLinearDimension(fChainAnchor, WorldPt(click.x, fChainAnchor.y), -offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "quickchain-h", plane, created);
		}
		else {
			AddLinearDimension(fChainAnchor, WorldPt(fChainAnchor.x, click.y), offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "quickchain-v", plane, created);
		}
		if (created > 0) {
			gSDK->EndUndoEvent();
			++fChainCreatedCount;
		}
		ViewPlane::End(plane);
		fChainAnchor = click;
		return;
	}
	if (fAnnotationMode == kAnnotationEnhanced) {
		if (selectedSources.empty()) {
			gSDK->AlertInform("请先选择圆弧、线对象或封闭对象，再使用加强标注模式。");
			return;
		}
		ViewPlane::SViewPlane plane = BeginViewPlaneForSources(selectedSources);
		if (EndElevationPlaneWithAlert(plane, "标注加强模式仅支持平面/非立面视图，当前为标准立面视图，请切换到平面视图后重试。")) {
			return;
		}
		size_t count = 0;
		for (MCObjectHandle source : selectedSources) count += CreateEnhancedDimensionsForSource(source, plane);
		ViewPlane::End(plane);
		if (count == 0) gSDK->AlertInform("未找到可用于加强标注的几何对象。");
		return;
	}
	if (fAnnotationMode == kAnnotationCenters) {
		if (selectedSources.size() < 2) {
			gSDK->AlertInform("请至少选择两个对象，再使用中心点标注模式。");
			return;
		}
		ViewPlane::SViewPlane plane = BeginViewPlaneForSources(selectedSources);
		const size_t count = CreateSpacingDimensionsForSelection(selectedSources, plane);
		ViewPlane::End(plane);
		if (count == 0) gSDK->AlertInform("未找到可测量的中心到中心距离。");
		return;
	}
	if (fAnnotationMode == kAnnotationBoundaries || fAnnotationMode == kAnnotationSelection || fAnnotationMode == kAnnotationAuto || fAnnotationMode == kAnnotationLine) {
	if (!selectedSources.empty()) {
		ViewPlane::SViewPlane selectionPlane = BeginViewPlaneForSources(selectedSources);
		SSelectionDimensionResult result;
		if (fAnnotationMode == kAnnotationBoundaries) {
			result.dimensionCount = CreateBoundaryDimensionsForSelection(selectedSources, selectionPlane);
		}
		else if (fAnnotationMode == kAnnotationLine) {
			for (MCObjectHandle source : selectedSources) {
				SLineMeasurement line;
				if (GetLineMeasurement(source, line)) result.dimensionCount += CreateDimensionsForSource(source, selectionPlane);
			}
		}
		else {
			result = CreateDimensionsForSelection(selectedSources, selectionPlane);
		}
		ViewPlane::End(selectionPlane);
		if (result.dimensionCount == 0) {
			gSDK->AlertInform("未找到可测量的水平或垂直范围。");
		}
		return;
	}
	}

	// Manual block positioning with two-point interaction
	if (fAnnotationMode == kAnnotationManualBlock && this->GetToolPointsCount() >= 2) {
		VWPoint2D firstPoint = this->GetToolPt2D(0);
		VWAD_RUNTIME_TRACE("tool-complete manual-block pt1=" + FormatPoint(WorldPt(firstPoint.x, firstPoint.y)));

		MCObjectHandle sourceObject = nullptr;
		if (!selectedSources.empty()) {
			sourceObject = selectedSources.front();
			VWAD_RUNTIME_TRACE("tool-complete manual-block using pre-selected " + DescribeObject(sourceObject));
		}
		else {
			const WorldPt searchPoint(firstPoint.x, firstPoint.y);
			gSDK->ForEachObjectAtPoint(2, searchPoint, 10.0, [&sourceObject](MCObjectHandle h) {
				if (IsSupportedSource(h)) {
					sourceObject = h;
					return false;
				}
				return true;
			});
			VWAD_RUNTIME_TRACE("tool-complete manual-block found at point " + DescribeObject(sourceObject));
		}

		if (!IsSupportedSource(sourceObject)) {
			gSDK->AlertInform("第一个点未找到有效对象。请先点击对象，再点击标注放置位置。");
			return;
		}

		ViewPlane::SViewPlane manualPlane = BeginViewPlaneForSources({ sourceObject });
		if (EndElevationPlaneWithAlert(manualPlane, "符号定位标注仅支持平面/非立面视图，当前为标准立面视图，请切换到平面视图后重试。")) {
			return;
		}

		VWPoint2D secondPoint = this->GetToolPt2D(1);
		VWAD_RUNTIME_TRACE("tool-complete manual-block pt2=" + FormatPoint(WorldPt(secondPoint.x, secondPoint.y)));

		WorldCube sourceBounds;
		gSDK->GetObjectCube(sourceObject, sourceBounds);
		const WorldCoord minX = sourceBounds.MinX();
		const WorldCoord minY = sourceBounds.MinY();
		const WorldCoord maxX = sourceBounds.MaxX();
		const WorldCoord maxY = sourceBounds.MaxY();
		const WorldCoord width = maxX - minX;
		const WorldCoord height = maxY - minY;
		const short sourceType = gSDK->GetObjectTypeN(sourceObject);
		const bool hasInsertionPoint = (sourceType == kSymbolNode || sourceType == kParametricNode);
		WorldPt insertionPoint(minX + width * 0.5, minY + height * 0.5);
		if (hasInsertionPoint) {
			TransformMatrix entityMatrix;
			gSDK->GetEntityMatrix(sourceObject, entityMatrix);
			insertionPoint = WorldPt(entityMatrix.P().x, entityMatrix.P().y);
		}
		VWAD_RUNTIME_TRACE("tool-complete manual-block insertionPoint=" + FormatPoint(insertionPoint));

		if (width <= kGeometryTolerance && height <= kGeometryTolerance) {
			gSDK->AlertInform("所选对象没有可测量的范围。");
			ViewPlane::End(manualPlane);
			return;
		}

		gSDK->SetUndoMethod(kUndoSwapObjects);
		size_t createdCount = 0;

		const WorldCoord dx = secondPoint.x - (minX + maxX) * 0.5;
		const WorldCoord dy = secondPoint.y - (minY + maxY) * 0.5;
		const bool isHorizontal = std::abs(dx) <= std::abs(dy);

		if (hasInsertionPoint && width > kGeometryTolerance && height > kGeometryTolerance) {
			if (isHorizontal) {
				const WorldCoord offset = secondPoint.y - minY;
				VWAD_RUNTIME_TRACE("tool-complete manual-insert horizontal insertionPoint=" + FormatPoint(insertionPoint));
				if (insertionPoint.x - minX > kGeometryTolerance) {
					AddLinearDimension(WorldPt(insertionPoint.x, minY), WorldPt(minX, minY), offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "manual-insert-left", manualPlane, createdCount);
				}
				if (maxX - insertionPoint.x > kGeometryTolerance) {
					AddLinearDimension(WorldPt(insertionPoint.x, minY), WorldPt(maxX, minY), offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "manual-insert-right", manualPlane, createdCount);
				}
			}
			else {
				const WorldCoord offset = secondPoint.x - minX;
				VWAD_RUNTIME_TRACE("tool-complete manual-insert vertical insertionPoint=" + FormatPoint(insertionPoint));
				if (insertionPoint.y - minY > kGeometryTolerance) {
					AddLinearDimension(WorldPt(minX, insertionPoint.y), WorldPt(minX, minY), offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "manual-insert-bottom", manualPlane, createdCount);
				}
				if (maxY - insertionPoint.y > kGeometryTolerance) {
					AddLinearDimension(WorldPt(minX, insertionPoint.y), WorldPt(minX, maxY), offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "manual-insert-top", manualPlane, createdCount);
				}
			}
		}
		else if (isHorizontal && width > kGeometryTolerance) {
			const WorldCoord offset = secondPoint.y - minY;
			VWAD_RUNTIME_TRACE("tool-complete manual-block horizontal offset=" + std::to_string(offset));
			AddLinearDimension(WorldPt(minX, minY), WorldPt(maxX, minY), offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "manual-horizontal", manualPlane, createdCount);
		}
		else if (!isHorizontal && height > kGeometryTolerance) {
			const WorldCoord offset = secondPoint.x - maxX;
			VWAD_RUNTIME_TRACE("tool-complete manual-block vertical offset=" + std::to_string(offset));
			AddLinearDimension(WorldPt(maxX, minY), WorldPt(maxX, maxY), offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "manual-vertical", manualPlane, createdCount);
		}

		if (createdCount > 0) gSDK->EndUndoEvent();
		ViewPlane::End(manualPlane);

		if (createdCount == 0) {
			gSDK->AlertInform("无法在指定位置创建标注。");
		}
		return;
	}

	MCObjectHandle sourceObject = nullptr;
	short overPart = 0;
	SintptrT code = 0;
	::GS_TrackTool(gCBP, sourceObject, overPart, code);
	VWAD_RUNTIME_TRACE("tool-complete track overPart=" + std::to_string(overPart)
		+ " code=" + std::to_string(code) + " " + DescribeObject(sourceObject));
	if (!sourceObject) {
		sourceObject = gSDK->FirstSelectedObject();
		VWAD_RUNTIME_TRACE("tool-complete selection fallback " + DescribeObject(sourceObject));
	}

	if (!IsSupportedSource(sourceObject)) {
		VWAD_RUNTIME_TRACE("tool-complete rejected source");
		gSDK->AlertInform("请点击符号、灯具、线对象、二维对象或三维对象。");
		return;
	}
	if (fAnnotationMode == kAnnotationClosedSpace && !IsClosedSpaceSource(sourceObject)) {
		gSDK->AlertInform("封闭空间模式请点击封闭多边形或封闭多段线。");
		return;
	}

	ViewPlane::SViewPlane clickPlane = BeginViewPlaneForSources({ sourceObject });
	const size_t clickDimensionCount = (fAnnotationMode == kAnnotationEnhanced)
		? CreateEnhancedDimensionsForSource(sourceObject, clickPlane)
		: CreateDimensionsForSource(sourceObject, clickPlane);
	ViewPlane::End(clickPlane);
	if (clickDimensionCount == 0) {
		VWAD_RUNTIME_TRACE("tool-complete dimension creation failed");
		gSDK->AlertInform("未找到可测量的水平或垂直范围。");
	}
}

Sint32 CAutoDimensionObjDefTool_EventSink::OnDefaultEvent(ToolMessage* message)
{
	if (message && message->fAction == ToolMessage::kAction_OnEscapeKeyWithNoToolPts) {
		if (fChainActive) {
			fChainActive = false;
			fChainCreatedCount = 0;
			gSDK->AlertInform("快速连续标注已结束。");
			return kToolSpecialKeyEventHandled;
		}
	}
	return VWTool_EventSink::OnDefaultEvent(message);
}
