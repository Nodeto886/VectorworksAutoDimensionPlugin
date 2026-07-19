#include "StdAfx.h"

#include "AutoDimensionObj.h"
#include "../../../../include/vwad/SDKComplexGeometry.h"

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
	static const char* kRuntimeTracePath = "C:\\Users\\keepl\\Downloads\\VectorworksAutoDimensionPlugin\\vw-autodim-runtime-2025.txt";
	static constexpr short kLinearDimensionTypeOrtho = 0;
	static constexpr short kLinearDimensionTypeAligned = 1;
	static constexpr size_t kDimensionModeGroup = 0;
	static constexpr size_t kDimensionModeSpacing = 1;
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

	static bool IsSupportedSource(MCObjectHandle object)
	{
		return object &&
			gSDK->GetObjectTypeN(object) != dimHeaderNode &&
			!VWParametricObj::IsParametricObject(object, "KeeplAutoDimTestObj");
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

	static void ApplyDimensionPresentation(MCObjectHandle dimension, const char* traceName)
	{
		const Boolean textSizeSet = gSDK->SetObjectVariable(
			dimension,
			ovDimTextSizeInPoints,
			TVariableBlock(static_cast<Real64>(kDimensionTextSizePoints)));
		gSDK->ResetObject(dimension);
		WriteRuntimeTrace(std::string("dimension-tool presentation ") + traceName
			+ " textPoints=" + std::to_string(kDimensionTextSizePoints)
			+ " set=" + (textSizeSet ? "true" : "false"));
	}

	static MCObjectHandle AddLinearDimension(const WorldPt& p1, const WorldPt& p2, WorldCoord startOffset, const Vector2& direction, short dimensionType, const char* traceName, size_t& ioCreatedCount)
	{
		MCObjectHandle dimension = gSDK->CreateLinearDimension(p1, p2, startOffset, 0.0, direction, dimensionType);
		if (dimension) {
			ApplyDimensionPresentation(dimension, traceName);
			gSDK->AddAfterSwapObject(dimension);
			++ioCreatedCount;
			WriteRuntimeTrace(std::string("dimension-tool created ") + traceName + " " + DescribeObject(dimension));
		}
		else {
			WriteRuntimeTrace(std::string("dimension-tool failed ") + traceName);
		}
		return dimension;
	}

	static MCObjectHandle AddAngleDimension(const WorldPt& center, const WorldPt& p1, const WorldPt& p2, WorldCoord startOffset, const char* traceName, size_t& ioCreatedCount)
	{
		MCObjectHandle dimension = gSDK->CreateAngleDimension(center, p1, p2, startOffset);
		if (dimension) {
			ApplyDimensionPresentation(dimension, traceName);
			gSDK->AddAfterSwapObject(dimension);
			++ioCreatedCount;
			WriteRuntimeTrace(std::string("dimension-tool created ") + traceName + " " + DescribeObject(dimension));
		}
		else {
			WriteRuntimeTrace(std::string("dimension-tool failed ") + traceName);
		}
		return dimension;
	}

	static size_t CreateDimensionsForSource(MCObjectHandle sourceObject)
	{
		if (!IsSupportedSource(sourceObject)) {
			WriteRuntimeTrace("dimension-tool rejected source");
			return 0;
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
			AddLinearDimension(leftBottom, rightBottom, -offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "horizontal", createdCount);
		}
		if (shouldCreateOverall && height > kGeometryTolerance) {
			AddLinearDimension(rightBottom, rightTop, offset, Vector2(0.0, 0.0), kLinearDimensionTypeOrtho, "vertical", createdCount);
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
				createdCount);

			WorldPt angleCenter;
			WorldPt angleP1;
			WorldPt angleP2;
			if (GetLineAngleDefinition(lineMeasurement, offset, angleCenter, angleP1, angleP2)) {
				AddAngleDimension(angleCenter, angleP1, angleP2, offset * 1.25, "angle", createdCount);
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
						AddAngleDimension(angleCenter, angleP1, angleP2, offset * 1.25, "dominant-angle", createdCount);
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

	static SSelectionDimensionResult CreateDimensionsForSelection(const std::vector<MCObjectHandle>& selectedSources)
	{
		SSelectionDimensionResult result;
		result.sourceCount = selectedSources.size();
		for (MCObjectHandle sourceObject : selectedSources) {
			result.dimensionCount += CreateDimensionsForSource(sourceObject);
		}

		WriteRuntimeTrace("selection-batch selected=" + std::to_string(gSDK->NumSelectedObjects())
			+ " sources=" + std::to_string(result.sourceCount)
			+ " dimensions=" + std::to_string(result.dimensionCount));
		return result;
	}

	struct SSpacingSource
	{
		WorldPt center;
		WorldCoord extent = 0.0;
	};

	static bool GetSpacingSource(MCObjectHandle sourceObject, SSpacingSource& outSource)
	{
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

	static size_t CreateSpacingDimensionsForSelection(const std::vector<MCObjectHandle>& selectedSources)
	{
		std::vector<SSpacingSource> spacingSources;
		for (MCObjectHandle sourceObject : selectedSources) {
			SSpacingSource spacingSource;
			if (GetSpacingSource(sourceObject, spacingSource)) {
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
	TXStringArray images;
	images.Append("KeeplAutoDimTest/Images/KeeplAutoDimTestObjTool.png");
	images.Append("Vectorworks/Images/ModeViewBar/Line_Button2.png");
	pModeBarInitProvider->AddRadioModeGroup(fDimensionMode, images);

	VectorWorks::TVWModeBarButtonHelpArray buttonHelp;
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Object dimensions", "Dimension every selected object independently.", VectorWorks::eModeBarButtonType_RadioMode));
	buttonHelp.Append(VectorWorks::SModeBarButtonHelp("Fixture spacing", "Dimension center-to-center spacing between adjacent selected fixtures.", VectorWorks::eModeBarButtonType_RadioMode));
	gSDK->SetModeBarButtonsText(buttonHelp);
	WriteRuntimeTrace("tool-setup mode=" + std::to_string(fDimensionMode));
	return result;
}

void CAutoDimensionObjDefTool_EventSink::DoSetDown(bool bRestore, const IToolModeBarInitProvider* pModeBarInitProvider)
{
	VWTool_EventSink::DoSetDown(bRestore, pModeBarInitProvider);
}

void CAutoDimensionObjDefTool_EventSink::DoModeEvent(size_t modeGroupID, size_t newButtonID, size_t oldButtonID)
{
	(void)oldButtonID;
	if (modeGroupID == kDimensionModeGroup) {
		fDimensionMode = newButtonID;
		WriteRuntimeTrace("tool-mode changed=" + std::to_string(fDimensionMode));
	}
}

void CAutoDimensionObjDefTool_EventSink::HandleComplete()
{
	const std::vector<MCObjectHandle> selectedSources = CollectSelectedSources();
	if (fDimensionMode == kDimensionModeSpacing) {
		if (selectedSources.size() < 2) {
			WriteRuntimeTrace("tool-complete spacing rejected selection");
			gSDK->AlertInform("Select at least two fixtures before using Fixture Spacing mode.");
			return;
		}
		if (CreateSpacingDimensionsForSelection(selectedSources) == 0) {
			gSDK->AlertInform("No measurable center-to-center spacing was found.");
		}
		return;
	}
	if (!selectedSources.empty()) {
		const SSelectionDimensionResult result = CreateDimensionsForSelection(selectedSources);
		if (result.dimensionCount == 0) {
			gSDK->AlertInform("No measurable horizontal or vertical extent was found.");
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

	if (CreateDimensionsForSource(sourceObject) == 0) {
		WriteRuntimeTrace("tool-complete dimension creation failed");
		gSDK->AlertInform("No measurable horizontal or vertical extent was found.");
	}
}
