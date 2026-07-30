#include "StdAfx.h"

#include "AutoDimensionObj.h"
#include "../../../../include/vwad/SDKComplexGeometry.h"
#include "../../../../include/vwad/SDKViewPlane.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

using namespace AutoDimensionPlugin;

namespace AutoDimensionPlugin
{
	static const TXString kCategoryUniversalName = "KeeplAutoDimTestCategory";
	static const TXString kCategoryLocalizedName = "Keepl AutoDim Test";
	static const TXString kOverallWidth = "AD_OverallWidth";
	static const TXString kOverallHeight = "AD_OverallHeight";
	static const TXString kOverallDepth = "AD_OverallDepth";
	static const TXString kSourceUUIDParam = "SourceUUID";
	static const char* kRuntimeTracePath =
#ifdef _WINDOWS
		"C:\\Users\\keepl\\Downloads\\VectorworksAutoDimensionPlugin\\vw-autodim-runtime-2026.txt";
#else
		"/tmp/vw-autodim-runtime-2026.txt";
#endif
	static constexpr short kLinearDimensionTypeOrtho = 0;
	static constexpr short kLinearDimensionTypeAligned = 1;
	static constexpr size_t kAnnotationModeGroup = 0;
	static constexpr size_t kEditModeGroup = 1;
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
	static constexpr double kDimensionTextSizePoints = 9.0;
	static constexpr double kPi = 3.14159265358979323846;

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
		const Boolean textSizeSet = gSDK->SetObjectVariable(
			dimension,
			ovDimTextSizeInPoints,
			TVariableBlock(static_cast<Real64>(kDimensionTextSizePoints)));
		gSDK->ResetObject(dimension);
		WriteRuntimeTrace(std::string("dimension-tool presentation ") + traceName
			+ " plane=" + plane.name
			+ " textPoints=" + std::to_string(kDimensionTextSizePoints)
			+ " set=" + (textSizeSet ? "true" : "false"));
	}

	static MCObjectHandle AddLinearDimension(const WorldPt& p1, const WorldPt& p2, WorldCoord startOffset, const Vector2& direction, short dimensionType, const char* traceName, const ViewPlane::SViewPlane& plane, size_t& ioCreatedCount)
	{
		MCObjectHandle dimension = gSDK->CreateLinearDimension(p1, p2, startOffset, 0.0, direction, dimensionType);
		if (dimension) {
			ApplyDimensionPresentation(dimension, plane, traceName);
			gSDK->AddAfterSwapObject(dimension);
			++ioCreatedCount;
			WriteRuntimeTrace(std::string("dimension-tool created ") + traceName + " " + DescribeObject(dimension));
		}
		else {
			WriteRuntimeTrace(std::string("dimension-tool failed ") + traceName);
		}
		return dimension;
	}

	static MCObjectHandle AddAngleDimension(const WorldPt& center, const WorldPt& p1, const WorldPt& p2, WorldCoord startOffset, const char* traceName, const ViewPlane::SViewPlane& plane, size_t& ioCreatedCount)
	{
		MCObjectHandle dimension = gSDK->CreateAngleDimension(center, p1, p2, startOffset);
		if (dimension) {
			ApplyDimensionPresentation(dimension, plane, traceName);
			gSDK->AddAfterSwapObject(dimension);
			++ioCreatedCount;
			WriteRuntimeTrace(std::string("dimension-tool created ") + traceName + " " + DescribeObject(dimension));
		}
		else {
			WriteRuntimeTrace(std::string("dimension-tool failed ") + traceName);
		}
		return dimension;
	}

	static MCObjectHandle AddCircularDimension(const WorldPt& center, const WorldPt& end, WorldCoord startOffset, bool radius, const char* traceName, const ViewPlane::SViewPlane& plane, size_t& ioCreatedCount)
	{
		const WorldRect box(center, std::hypot(end.x - center.x, end.y - center.y));
		MCObjectHandle dimension = gSDK->CreateCircularDimension(center, end, startOffset, 0.0, box, radius);
		if (dimension) {
			ApplyDimensionPresentation(dimension, plane, traceName);
			gSDK->AddAfterSwapObject(dimension);
			++ioCreatedCount;
			WriteRuntimeTrace(std::string("dimension-tool created ") + traceName + " " + DescribeObject(dimension));
		}
		return dimension;
	}

	static MCObjectHandle AddArcLengthDimension(const WorldPt& start, const WorldPt& end, const WorldPt& center, WorldCoord startOffset, bool clockwise, const char* traceName, const ViewPlane::SViewPlane& plane, size_t& ioCreatedCount)
	{
		MCObjectHandle dimension = gSDK->CreateArcLengthDimension(start, end, center, startOffset, clockwise, false, true);
		if (dimension) {
			ApplyDimensionPresentation(dimension, plane, traceName);
			gSDK->AddAfterSwapObject(dimension);
			++ioCreatedCount;
			WriteRuntimeTrace(std::string("dimension-tool created ") + traceName + " " + DescribeObject(dimension));
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

	static bool SetDimensionVariable(MCObjectHandle object, short selector, const TVariableBlock& value)
	{
		if (!object || gSDK->GetObjectTypeN(object) != dimHeaderNode) return false;
		const bool result = gSDK->SetObjectVariable(object, selector, value);
		if (result) gSDK->ResetObject(object);
		return result;
	}

	static size_t CreateEnhancedDimensionsForSource(MCObjectHandle sourceObject, const ViewPlane::SViewPlane& plane)
	{
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
				const WorldCoord offset = std::max<WorldCoord>(25.0, extent * 0.25);
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
				const WorldCoord offset = std::max<WorldCoord>(25.0, maxRadius * 0.25);

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
				WriteRuntimeTrace("enhanced-oval isCircle=" + std::to_string(isCircle) + " radiusX=" + std::to_string(radiusX) + " radiusY=" + std::to_string(radiusY) + " created=" + std::to_string(createdCount));
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
				const WorldCoord baseOffset = std::max<WorldCoord>(25.0, extent * 0.15);

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
				WriteRuntimeTrace("enhanced-polyline-arcs arcCount=" + std::to_string(arcEdges.size()) + " created=" + std::to_string(createdCount));
				return createdCount;
			}
		}

		return CreateDimensionsForSource(sourceObject, plane);
	}

	static size_t CreateContinuousDimensions(const std::vector<MCObjectHandle>& sources, const ViewPlane::SViewPlane& plane)
	{
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
			AddLinearDimension(segment.start, segment.end, -std::max<WorldCoord>(25.0, segment.length * 0.15), Vector2(dx / segment.length, dy / segment.length), kLinearDimensionTypeAligned, "continuous", plane, createdCount);
		}
		if (createdCount > 0) gSDK->EndUndoEvent();
		return createdCount;
	}

	static size_t CreateIntersectionDimensions(const std::vector<MCObjectHandle>& sources, const ViewPlane::SViewPlane& plane)
	{
		std::vector<SLineMeasurement> lines;
		for (MCObjectHandle source : sources) {
			SLineMeasurement line;
			if (GetLineMeasurement(source, line)) lines.push_back(line);
		}
		if (lines.size() < 2) return 0;
		size_t createdCount = 0;
		gSDK->SetUndoMethod(kUndoSwapObjects);
		std::vector<WorldPt> intersections;
		for (size_t first = 0; first < lines.size(); ++first) {
			for (size_t second = first + 1; second < lines.size(); ++second) {
				const SLineMeasurement& a = lines[first];
				const SLineMeasurement& b = lines[second];
				const double det = a.dx * b.dy - a.dy * b.dx;
				if (std::abs(det) <= kGeometryTolerance) continue;
				const double qx = b.start.x - a.start.x;
				const double qy = b.start.y - a.start.y;
				const double t = (qx * b.dy - qy * b.dx) / det;
				const double u = (qx * a.dy - qy * a.dx) / det;
				if (t < -kGeometryTolerance || t > 1.0 + kGeometryTolerance || u < -kGeometryTolerance || u > 1.0 + kGeometryTolerance) continue;
				const WorldPt intersection(a.start.x + t * a.dx, a.start.y + t * a.dy);
				bool duplicate = false;
				for (const WorldPt& existing : intersections) {
					if (std::hypot(existing.x - intersection.x, existing.y - intersection.y) <= 1e-4) { duplicate = true; break; }
				}
				if (duplicate) continue;
				intersections.push_back(intersection);
				const double angleA = std::atan2(a.dy, a.dx);
				const double angleB = std::atan2(b.dy, b.dx);
				const WorldPt p1(intersection.x + std::cos(angleA) * 100.0, intersection.y + std::sin(angleA) * 100.0);
				const WorldPt p2(intersection.x + std::cos(angleB) * 100.0, intersection.y + std::sin(angleB) * 100.0);
				AddAngleDimension(intersection, p1, p2, 50.0 + static_cast<WorldCoord>(intersections.size()) * 20.0, "intersection-angle", plane, createdCount);
				if (intersections.size() >= 128) break;
			}
			if (intersections.size() >= 128) break;
		}
		if (createdCount > 0) gSDK->EndUndoEvent();
		return createdCount;
	}

	static size_t CreateBoundaryDimensionsForSelection(const std::vector<MCObjectHandle>& sources, const ViewPlane::SViewPlane& plane)
	{
		if (sources.empty()) return 0;
		const WorldCube cube = GetSourcesCube(sources);
		ViewPlane::SPlanarBounds bounds;
		if (plane.planar) ViewPlane::AddCubeCorners(plane, cube, bounds);
		else { bounds.Add(WorldPt(cube.MinX(), cube.MinY())); bounds.Add(WorldPt(cube.MaxX(), cube.MaxY())); }
		if (!bounds.valid) return 0;
		const WorldCoord extent = std::max<WorldCoord>(bounds.Width(), bounds.Height());
		const WorldCoord offset = std::max<WorldCoord>(25.0, extent * 0.15);
		size_t createdCount = 0;
		gSDK->SetUndoMethod(kUndoSwapObjects);
		if (bounds.Width() > kGeometryTolerance) AddLinearDimension(WorldPt(bounds.minU, bounds.minV), WorldPt(bounds.maxU, bounds.minV), -offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "selection-boundary-width", plane, createdCount);
		if (bounds.Height() > kGeometryTolerance) AddLinearDimension(WorldPt(bounds.maxU, bounds.minV), WorldPt(bounds.maxU, bounds.maxV), offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "selection-boundary-height", plane, createdCount);
		if (createdCount > 0) gSDK->EndUndoEvent();
		return createdCount;
	}

	static size_t EditSelectedDimensions(size_t editMode, const ViewPlane::SViewPlane& plane)
	{
		const std::vector<MCObjectHandle> dimensions = CollectSelectedDimensions();
		if (dimensions.empty()) return 0;

		gSDK->SetUndoMethod(kUndoSwapObjects);
		size_t changedCount = 0;
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
				const double dx = end.x - start.x;
				const double dy = end.y - start.y;
				const double length = std::hypot(dx, dy);
				if (length > kGeometryTolerance) {
					MCObjectHandle replacement = gSDK->CreateLinearDimension(start, end, 0.0, 0.0, Vector2(dx / length, dy / length), kLinearDimensionTypeAligned);
					if (replacement) {
						ViewPlane::ApplyPlanarRef(replacement, plane);
						gSDK->AddAfterSwapObject(replacement);
						gSDK->DeleteObject(dimension);
						++changedCount;
					}
				}
				break;
			}
			case kEditTrim:
			{
				// Find source geometry near dimension endpoints and set custom witness offsets
				const double dx = end.x - start.x;
				const double dy = end.y - start.y;
				const double length = std::hypot(dx, dy);
				if (length <= kGeometryTolerance) break;

				// Witness line direction is perpendicular to dimension line
				const WorldCoord witnessX = -dy / length;
				const WorldCoord witnessY = dx / length;

				WorldCoord currentOffset = 0.0;
				GetDimensionReal(dimension, ovDimStartOffset, currentOffset);

				// Search for source objects near the dimension endpoints
				std::vector<MCObjectHandle> sourceObjects;
				const WorldCoord searchRadius = std::max<WorldCoord>(10.0, length * 0.1);

				gSDK->ForEachObjectAtPoint(2, start, searchRadius, [&sourceObjects](MCObjectHandle h) {
					if (IsSupportedSource(h)) {
						sourceObjects.push_back(h);
						return true;
					}
					return true;
				});

				gSDK->ForEachObjectAtPoint(2, end, searchRadius, [&sourceObjects](MCObjectHandle h) {
					if (IsSupportedSource(h)) {
						bool alreadyAdded = false;
						for (MCObjectHandle existing : sourceObjects) {
							if (existing == h) {
								alreadyAdded = true;
								break;
							}
						}
						if (!alreadyAdded) sourceObjects.push_back(h);
					}
					return true;
				});

				if (sourceObjects.empty()) {
					// No sources found, just reset to 0
					if (SetDimensionVariable(dimension, ovDimStartOffset, TVariableBlock(static_cast<Real64>(0.0)))) ++changedCount;
					break;
				}

				// Collect all geometry points from source objects
				std::vector<WorldPt> geometryPoints;
				for (MCObjectHandle source : sourceObjects) {
					ComplexGeometry::SCollection geometry = ComplexGeometry::Collect(source);
					geometryPoints.insert(geometryPoints.end(), geometry.points.begin(), geometry.points.end());
				}

				if (geometryPoints.empty()) {
					if (SetDimensionVariable(dimension, ovDimStartOffset, TVariableBlock(static_cast<Real64>(0.0)))) ++changedCount;
					break;
				}

				// Project all geometry points onto the witness line direction
				// Find maximum projection from start and end points
				double maxStartProjection = 0.0;
				double maxEndProjection = 0.0;

				for (const WorldPt& pt : geometryPoints) {
					// Project point relative to start point
					const double startDx = pt.x - start.x;
					const double startDy = pt.y - start.y;
					const double startProj = startDx * witnessX + startDy * witnessY;

					// Project point relative to end point
					const double endDx = pt.x - end.x;
					const double endDy = pt.y - end.y;
					const double endProj = endDx * witnessX + endDy * witnessY;

					// We want maximum projection toward dimension line (same sign as offset)
					if (currentOffset >= 0.0) {
						maxStartProjection = std::max(maxStartProjection, startProj);
						maxEndProjection = std::max(maxEndProjection, endProj);
					}
					else {
						maxStartProjection = std::min(maxStartProjection, startProj);
						maxEndProjection = std::min(maxEndProjection, endProj);
					}
				}

				// Convert from world coordinates to page inches
				const double startOffsetPageInches = gSDK->CoordLengthToPageLengthN(std::abs(maxStartProjection));
				const double endOffsetPageInches = gSDK->CoordLengthToPageLengthN(std::abs(maxEndProjection));

				// Set custom witness offsets
				bool changed = false;
				changed |= gSDK->SetObjectVariable(dimension, ovDimWitnessOverride, TVariableBlock(static_cast<Sint16>(4)));
				changed |= gSDK->SetObjectVariable(dimension, ovDimCustStartWitOffset, TVariableBlock(static_cast<Real64>(startOffsetPageInches)));
				changed |= gSDK->SetObjectVariable(dimension, ovDimCustEndWitOffset, TVariableBlock(static_cast<Real64>(endOffsetPageInches)));

				if (changed) {
					gSDK->ResetObject(dimension);
					++changedCount;
					WriteRuntimeTrace("edit-trim dimension=" + DescribeObject(dimension)
						+ " sources=" + std::to_string(sourceObjects.size())
						+ " points=" + std::to_string(geometryPoints.size())
						+ " startOffset=" + std::to_string(startOffsetPageInches)
						+ " endOffset=" + std::to_string(endOffsetPageInches));
				}
				break;
			}
			case kEditAlign:
			{
				if (hasSharedOffset && SetDimensionVariable(dimension, ovDimStartOffset, TVariableBlock(static_cast<Real64>(sharedOffset)))) ++changedCount;
				break;
			}
			case kEditSplitExtend:
			{
				const double dx = end.x - start.x;
				const double dy = end.y - start.y;
				const double length = std::hypot(dx, dy);
				if (length > kGeometryTolerance) {
					const double extension = std::max<WorldCoord>(10.0, length * 0.05);
					const WorldPt midpoint((start.x + end.x) * 0.5, (start.y + end.y) * 0.5);
					const Vector2 direction(dx / length, dy / length);
					MCObjectHandle firstHalf = gSDK->CreateLinearDimension(start, midpoint, 0.0, 0.0, direction, kLinearDimensionTypeAligned);
					MCObjectHandle secondHalf = gSDK->CreateLinearDimension(midpoint, end, 0.0, 0.0, direction, kLinearDimensionTypeAligned);
					if (firstHalf && secondHalf) {
						ApplyDimensionPresentation(firstHalf, plane, "split-first");
						ApplyDimensionPresentation(secondHalf, plane, "split-second");
						gSDK->AddAfterSwapObject(firstHalf);
						gSDK->AddAfterSwapObject(secondHalf);
						gSDK->DeleteObject(dimension);
						changedCount += 2;
					}
					else {
						if (firstHalf) gSDK->DeleteObject(firstHalf);
						if (secondHalf) gSDK->DeleteObject(secondHalf);
						const WorldPt newStart(start.x - dx / length * extension, start.y - dy / length * extension);
						const WorldPt newEnd(end.x + dx / length * extension, end.y + dy / length * extension);
						if (SetDimensionVariable(dimension, ovDimStartPt, TVariableBlock(newStart)) && SetDimensionVariable(dimension, ovDimEndPt, TVariableBlock(newEnd))) ++changedCount;
					}
				}
				break;
			}
			case kEditTextDirection:
				if (SetDimensionVariable(dimension, ovDimTextRotation, TVariableBlock(static_cast<Sint16>(kHorVert)))) ++changedCount;
				break;
			case kEditPoints:
				if (start.x > end.x || (std::abs(start.x - end.x) <= kGeometryTolerance && start.y > end.y)) {
					if (SetDimensionVariable(dimension, ovDimStartPt, TVariableBlock(end)) && SetDimensionVariable(dimension, ovDimEndPt, TVariableBlock(start))) ++changedCount;
				}
				break;
			case kEditMerge:
				chainSources.push_back(dimension);
				break;
			case kEditAvoid:
			{
				// Multi-round collision detection and avoidance
				struct DimensionBoundsInfo {
					MCObjectHandle handle;
					WorldRect bounds;
					WorldCoord currentOffset;
					WorldCoord textOffset;
				};

				std::vector<DimensionBoundsInfo> dimensionBounds;

				// Collect all selected dimensions with their bounds
				for (MCObjectHandle dim : dimensions) {
					DimensionBoundsInfo info;
					info.handle = dim;
					if (gSDK->GetObjectBounds(dim, info.bounds)) {
						GetDimensionReal(dim, ovDimStartOffset, info.currentOffset);
						GetDimensionReal(dim, ovDimTextOffsetInCurrUnits, info.textOffset);
						dimensionBounds.push_back(info);
					}
				}

				if (dimensionBounds.size() < 2) {
					// Single dimension - just add fixed offset
					WorldCoord offset = 0.0;
					GetDimensionReal(dimension, ovDimStartOffset, offset);
					if (SetDimensionVariable(dimension, ovDimStartOffset, TVariableBlock(static_cast<Real64>(offset + 25.0)))) ++changedCount;
					break;
				}

				// Run multiple iterations to resolve collisions
				const size_t maxIterations = 8;
				const WorldCoord adjustmentStep = 15.0;
				size_t iterationCollisions = 0;

				for (size_t iteration = 0; iteration < maxIterations; ++iteration) {
					iterationCollisions = 0;

					// Check all pairs for overlaps
					for (size_t i = 0; i < dimensionBounds.size(); ++i) {
						for (size_t j = i + 1; j < dimensionBounds.size(); ++j) {
							const WorldRect& rectA = dimensionBounds[i].bounds;
							const WorldRect& rectB = dimensionBounds[j].bounds;

							// Check for overlap with small tolerance
							const WorldCoord tolerance = 2.0;
							const bool overlaps = !(rectA.right + tolerance < rectB.left ||
								rectB.right + tolerance < rectA.left ||
								rectA.bottom - tolerance > rectB.top ||
								rectB.bottom - tolerance > rectA.top);

							if (overlaps) {
								++iterationCollisions;

								// Determine adjustment strategy based on relative positions
								const WorldCoord centerAX = (rectA.left + rectA.right) * 0.5;
								const WorldCoord centerAY = (rectA.top + rectA.bottom) * 0.5;
								const WorldCoord centerBX = (rectB.left + rectB.right) * 0.5;
								const WorldCoord centerBY = (rectB.top + rectB.bottom) * 0.5;

								const WorldCoord dx = centerBX - centerAX;
								const WorldCoord dy = centerBY - centerAY;

								// Adjust dimension with smaller index (to maintain stability)
								MCObjectHandle dimToAdjust = dimensionBounds[i].handle;
								WorldCoord& offsetToAdjust = dimensionBounds[i].currentOffset;

								// Try adjusting offset first
								if (std::abs(dy) > std::abs(dx)) {
									// Vertical separation - adjust offset
									offsetToAdjust += (dy > 0.0) ? -adjustmentStep : adjustmentStep;
									gSDK->SetObjectVariable(dimToAdjust, ovDimStartOffset, TVariableBlock(static_cast<Real64>(offsetToAdjust)));
									gSDK->ResetObject(dimToAdjust);
									gSDK->GetObjectBounds(dimToAdjust, dimensionBounds[i].bounds);
								}
								else {
									// Horizontal separation - adjust text offset
									WorldCoord& textOffsetToAdjust = dimensionBounds[i].textOffset;
									textOffsetToAdjust += (dx > 0.0) ? -adjustmentStep : adjustmentStep;
									gSDK->SetObjectVariable(dimToAdjust, ovDimTextOffsetInCurrUnits, TVariableBlock(static_cast<Real64>(textOffsetToAdjust)));
									gSDK->SetObjectVariable(dimToAdjust, ovDimTextPosCalculated, TVariableBlock(false));
									gSDK->ResetObject(dimToAdjust);
									gSDK->GetObjectBounds(dimToAdjust, dimensionBounds[i].bounds);
								}
							}
						}
					}

					WriteRuntimeTrace("edit-avoid iteration=" + std::to_string(iteration)
						+ " collisions=" + std::to_string(iterationCollisions)
						+ " dimensions=" + std::to_string(dimensionBounds.size()));

					// Stop if no more collisions
					if (iterationCollisions == 0) {
						break;
					}
				}

				changedCount += dimensionBounds.size();
				WriteRuntimeTrace("edit-avoid completed iterations=" + std::to_string(std::min(maxIterations, dimensionBounds.size()))
					+ " finalCollisions=" + std::to_string(iterationCollisions)
					+ " adjusted=" + std::to_string(dimensionBounds.size()));
				break;
			}
			case kEditResetText:
				if (SetDimensionVariable(dimension, ovDimLeaderText, TVariableBlock(TXString())) && SetDimensionVariable(dimension, ovDimTrailerText, TVariableBlock(TXString())) && SetDimensionVariable(dimension, ovDimNoteText, TVariableBlock(TXString()))) ++changedCount;
				break;
			case kEditResetTextPosition:
				if (SetDimensionVariable(dimension, ovDimTextPosCalculated, TVariableBlock(true)) && SetDimensionVariable(dimension, ovDimTextRotation, TVariableBlock(static_cast<Sint16>(kAlign)))) ++changedCount;
				break;
			default:
				break;
			}
		}

		if (editMode == kEditMerge && chainSources.size() >= 2) {
			for (size_t index = 1; index < chainSources.size(); ++index) {
				MCObjectHandle chain = gSDK->CreateChainDimension(chainSources[index - 1], chainSources[index]);
				if (chain) {
					ViewPlane::ApplyPlanarRef(chain, plane);
					gSDK->AddAfterSwapObject(chain);
					++changedCount;
				}
			}
		}
		if (changedCount > 0) gSDK->EndUndoEvent();
		WriteRuntimeTrace("dimension-edit mode=" + std::to_string(editMode) + " selected=" + std::to_string(dimensions.size()) + " changed=" + std::to_string(changedCount));
		return changedCount;
	}

	// Front, back, left and right views measure the real Z range of the source instead
	// of the top view projection. The dimensions are created in the coordinates of the
	// measuring plane, so the horizontal dimension is the extent the camera actually
	// sees and the vertical dimension is the true model height.
	static size_t CreateElevationDimensionsForSource(MCObjectHandle sourceObject, const ViewPlane::SViewPlane& plane)
	{
		WorldCube objectBounds;
		gSDK->GetObjectCube(sourceObject, objectBounds);

		ViewPlane::SPlanarBounds cubeBounds;
		ViewPlane::AddCubeCorners(plane, objectBounds, cubeBounds);
		if (!cubeBounds.valid) {
			WriteRuntimeTrace("dimension-tool elevation bounds failed " + DescribeObject(sourceObject));
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
		const WorldCoord offset = std::max<WorldCoord>(25.0, std::max(width, height) * 0.15);

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
		WriteRuntimeTrace(trace.str());
		return createdCount;
	}

	static size_t CreateDimensionsForSource(MCObjectHandle sourceObject, const ViewPlane::SViewPlane& plane)
	{
		if (!IsSupportedSource(sourceObject)) {
			WriteRuntimeTrace("dimension-tool rejected source");
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
			WriteRuntimeTrace(geometryTrace.str());
		}
		if (hasLineMeasurement) {
			std::ostringstream lineTrace;
			lineTrace.precision(12);
			lineTrace << "dimension-tool line start=" << FormatPoint(lineMeasurement.start)
				<< " end=" << FormatPoint(lineMeasurement.end)
				<< " dx=" << lineMeasurement.dx
				<< " dy=" << lineMeasurement.dy
				<< " length=" << lineMeasurement.length
				<< " angle-degrees=" << lineMeasurement.angleDegrees;
			WriteRuntimeTrace(lineTrace.str());
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
		const WorldCoord offset = std::max<WorldCoord>(25.0, measuredExtent * 0.15);
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
				std::ostringstream orientedTrace;
				orientedTrace.precision(12);
				orientedTrace << "dimension-tool oriented width=" << orientedBounds.width
					<< " height=" << orientedBounds.height;
				WriteRuntimeTrace(orientedTrace.str());

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
		WriteRuntimeTrace(trace.str());
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

		WriteRuntimeTrace("selection-batch selected=" + std::to_string(gSDK->NumSelectedObjects())
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
		WriteRuntimeTrace(ViewPlane::Describe(plane));
		return plane;
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
			WriteRuntimeTrace("spacing-batch requires at least two measurable sources");
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
		WriteRuntimeTrace("spacing-batch sources=" + std::to_string(spacingSources.size())
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
	const bool result = VWTool_EventSink::DoSetUp(bRestore, pModeBarInitProvider);
	TXStringArray annotationImages;
	const std::array<const char*, 10> annotationIconNames = {
		"ModeAuto.png", "ModeContinuous.png", "ModeLine.png", "ModeManualBlock.png", "ModeIntersection.png",
		"ModeSelection.png", "ModeCenters.png", "ModeBoundaries.png", "ModeClosedSpace.png", "ModeEnhanced.png"
	};
	for (const char* iconName : annotationIconNames) {
		TXString iconPath = "KeeplAutoDimTest/Images/";
		iconPath += iconName;
		annotationImages.Append(iconPath);
	}
	pModeBarInitProvider->AddRadioModeGroup(fAnnotationModeGroup, annotationImages);

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
	pModeBarInitProvider->AddRadioModeGroup(fEditModeGroup, editImages);

	VectorWorks::TVWModeBarButtonHelpArray buttonHelp;
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Auto recognize", "Automatically dimension selected objects.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Continuous", "Create consecutive aligned dimensions.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Line object", "Dimension line endpoints and angle.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Manual block", "Place a block dimension from the clicked object.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Intersections", "Dimension the angle at intersecting lines.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Selection", "Dimension every selected object independently.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Centers", "Dimension centers of multiple objects.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Boundaries", "Dimension boundaries of multiple blocks.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Closed space", "Dimension a clicked closed space.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Enhanced", "Create angle, radius, diameter and arc length dimensions.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Edit: none", "Create mode.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Convert", "Convert selected dimensions to aligned dimensions.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Trim", "Trim selected dimension witnesses to the source bounds.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Align", "Align selected dimensions to a common line.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Split / extend", "Split or extend selected dimension endpoints.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Text direction", "Correct selected dimension text direction.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Dimension points", "Correct selected dimension points.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Merge", "Create chain dimensions from adjacent selected dimensions.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Avoid text", "Automatically move text outside overlapping dimension boxes.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Reset text", "Reset selected dimension text.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Reset text position", "Reset selected dimension text position.", VectorWorks::eModeBarButtonType_RadioMode));
	gSDK->SetModeBarButtonsText(buttonHelp);
	WriteRuntimeTrace("tool-setup annotation-mode=" + std::to_string(fAnnotationMode) + " edit-mode=" + std::to_string(fEditMode));
	return result;
}

void CAutoDimensionObjDefTool_EventSink::DoSetDown(bool bRestore, const IToolModeBarInitProvider* pModeBarInitProvider)
{
	VWTool_EventSink::DoSetDown(bRestore, pModeBarInitProvider);
}

void CAutoDimensionObjDefTool_EventSink::DoModeEvent(size_t modeGroupID, size_t newButtonID, size_t oldButtonID)
{
	(void)oldButtonID;
	if (modeGroupID == kAnnotationModeGroup) {
		fAnnotationMode = newButtonID;
		WriteRuntimeTrace("tool-annotation-mode changed=" + std::to_string(fAnnotationMode));
	}
	else if (modeGroupID == kEditModeGroup) {
		fEditMode = newButtonID;
		WriteRuntimeTrace("tool-edit-mode changed=" + std::to_string(fEditMode));
	}
}

TToolStatus CAutoDimensionObjDefTool_EventSink::GetStatus(const IToolStatusProvider* pStatusProvider)
{
	if (fAnnotationMode == kAnnotationManualBlock) {
		fResult = pStatusProvider->GetTwoPointToolStatus();
	}
	else {
		fResult = pStatusProvider->GetOnePointToolStatus();
	}
	this->Default();
	return fResult;
}

void CAutoDimensionObjDefTool_EventSink::HandleComplete()
{
	const std::vector<MCObjectHandle> selectedDimensions = CollectSelectedDimensions();
	if (fEditMode != kEditNone) {
		if (selectedDimensions.empty()) {
			gSDK->AlertInform("Select one or more dimension objects before using an edit mode.");
			return;
		}
		ViewPlane::SViewPlane editPlane = BeginViewPlaneForSources(selectedDimensions);
		const size_t changedCount = EditSelectedDimensions(fEditMode, editPlane);
		ViewPlane::End(editPlane);
		if (changedCount == 0) gSDK->AlertInform("The selected dimensions could not be edited.");
		return;
	}

	const std::vector<MCObjectHandle> selectedSources = CollectSelectedSources();
	if (fAnnotationMode == kAnnotationContinuous) {
		if (selectedSources.empty()) {
			gSDK->AlertInform("Select line or path objects before using Continuous mode.");
			return;
		}
		ViewPlane::SViewPlane plane = BeginViewPlaneForSources(selectedSources);
		const size_t count = CreateContinuousDimensions(selectedSources, plane);
		ViewPlane::End(plane);
		if (count == 0) gSDK->AlertInform("No measurable path segments were found.");
		return;
	}
	if (fAnnotationMode == kAnnotationIntersection) {
		ViewPlane::SViewPlane plane = BeginViewPlaneForSources(selectedSources);
		const size_t count = CreateIntersectionDimensions(selectedSources, plane);
		ViewPlane::End(plane);
		if (count == 0) gSDK->AlertInform("Select at least two intersecting line objects.");
		return;
	}
	if (fAnnotationMode == kAnnotationEnhanced) {
		if (selectedSources.empty()) {
			gSDK->AlertInform("Select an arc, line, or closed object before using Enhanced mode.");
			return;
		}
		ViewPlane::SViewPlane plane = BeginViewPlaneForSources(selectedSources);
		size_t count = 0;
		for (MCObjectHandle source : selectedSources) count += CreateEnhancedDimensionsForSource(source, plane);
		ViewPlane::End(plane);
		if (count == 0) gSDK->AlertInform("No enhanced dimension geometry was found.");
		return;
	}
	if (fAnnotationMode == kAnnotationCenters) {
		if (selectedSources.size() < 2) {
			gSDK->AlertInform("Select at least two objects before using Centers mode.");
			return;
		}
		ViewPlane::SViewPlane plane = BeginViewPlaneForSources(selectedSources);
		const size_t count = CreateSpacingDimensionsForSelection(selectedSources, plane);
		ViewPlane::End(plane);
		if (count == 0) gSDK->AlertInform("No measurable center-to-center spacing was found.");
		return;
	}
	if (fAnnotationMode == kAnnotationBoundaries || fAnnotationMode == kAnnotationSelection || fAnnotationMode == kAnnotationAuto || fAnnotationMode == kAnnotationLine || fAnnotationMode == kAnnotationManualBlock || fAnnotationMode == kAnnotationClosedSpace) {
	if (!selectedSources.empty()) {
		ViewPlane::SViewPlane selectionPlane = BeginViewPlaneForSources(selectedSources);
		SSelectionDimensionResult result;
		if (fAnnotationMode == kAnnotationClosedSpace) {
			for (MCObjectHandle source : selectedSources) {
				if (IsClosedSpaceSource(source)) result.dimensionCount += CreateDimensionsForSource(source, selectionPlane);
			}
		}
		else if (fAnnotationMode == kAnnotationBoundaries) {
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
			gSDK->AlertInform("No measurable horizontal or vertical extent was found.");
		}
		return;
	}
	}

	// Manual block positioning with two-point interaction
	if (fAnnotationMode == kAnnotationManualBlock && this->GetToolPointsCount() >= 2) {
		VWPoint2D firstPoint = this->GetToolPt2D(0);
		VWPoint2D secondPoint = this->GetToolPt2D(1);
		WriteRuntimeTrace("tool-complete manual-block pt1=" + FormatPoint(WorldPt(firstPoint.x, firstPoint.y))
			+ " pt2=" + FormatPoint(WorldPt(secondPoint.x, secondPoint.y)));

		MCObjectHandle sourceObject = nullptr;
		if (!selectedSources.empty()) {
			sourceObject = selectedSources.front();
			WriteRuntimeTrace("tool-complete manual-block using pre-selected " + DescribeObject(sourceObject));
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
			WriteRuntimeTrace("tool-complete manual-block found at point " + DescribeObject(sourceObject));
		}

		if (!IsSupportedSource(sourceObject)) {
			gSDK->AlertInform("No valid object found at first point. Click an object first, then click where to place the dimension.");
			return;
		}

		WorldCube sourceBounds;
		gSDK->GetObjectCube(sourceObject, sourceBounds);
		const WorldCoord minX = sourceBounds.MinX();
		const WorldCoord minY = sourceBounds.MinY();
		const WorldCoord maxX = sourceBounds.MaxX();
		const WorldCoord maxY = sourceBounds.MaxY();
		const WorldCoord width = maxX - minX;
		const WorldCoord height = maxY - minY;

		if (width <= kGeometryTolerance && height <= kGeometryTolerance) {
			gSDK->AlertInform("Selected object has no measurable extent.");
			return;
		}

		ViewPlane::SViewPlane manualPlane = BeginViewPlaneForSources({ sourceObject });
		gSDK->SetUndoMethod(kUndoSwapObjects);
		size_t createdCount = 0;

		const WorldCoord dx = secondPoint.x - (minX + maxX) * 0.5;
		const WorldCoord dy = secondPoint.y - (minY + maxY) * 0.5;
		const bool isHorizontal = std::abs(dx) <= std::abs(dy);

		if (isHorizontal && width > kGeometryTolerance) {
			const WorldCoord offset = secondPoint.y - minY;
			WriteRuntimeTrace("tool-complete manual-block horizontal offset=" + std::to_string(offset));
			AddLinearDimension(WorldPt(minX, minY), WorldPt(maxX, minY), offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "manual-horizontal", manualPlane, createdCount);
		}
		else if (!isHorizontal && height > kGeometryTolerance) {
			const WorldCoord offset = secondPoint.x - maxX;
			WriteRuntimeTrace("tool-complete manual-block vertical offset=" + std::to_string(offset));
			AddLinearDimension(WorldPt(maxX, minY), WorldPt(maxX, maxY), offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "manual-vertical", manualPlane, createdCount);
		}

		if (createdCount > 0) gSDK->EndUndoEvent();
		ViewPlane::End(manualPlane);

		if (createdCount == 0) {
			gSDK->AlertInform("Could not create dimension at the specified location.");
		}
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
	if (fAnnotationMode == kAnnotationClosedSpace && !IsClosedSpaceSource(sourceObject)) {
		gSDK->AlertInform("Click a closed polygon or polyline for Closed space mode.");
		return;
	}

	ViewPlane::SViewPlane clickPlane = BeginViewPlaneForSources({ sourceObject });
	const size_t clickDimensionCount = (fAnnotationMode == kAnnotationEnhanced)
		? CreateEnhancedDimensionsForSource(sourceObject, clickPlane)
		: CreateDimensionsForSource(sourceObject, clickPlane);
	ViewPlane::End(clickPlane);
	if (clickDimensionCount == 0) {
		WriteRuntimeTrace("tool-complete dimension creation failed");
		gSDK->AlertInform("No measurable horizontal or vertical extent was found.");
	}
}
