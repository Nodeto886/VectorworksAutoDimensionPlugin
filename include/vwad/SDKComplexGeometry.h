#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <vector>

#include "AutoDimensionAlgorithms.h"

namespace AutoDimensionPlugin
{
	namespace ComplexGeometry
	{
		static constexpr double kTolerance = 1e-6;
		static constexpr double kPi = 3.14159265358979323846;
		static constexpr size_t kMaximumDepth = 8;
		static constexpr size_t kMaximumObjects = 512;
		static constexpr size_t kMaximumPoints = 512;
		static constexpr size_t kMaximumSegments = 256;
		static constexpr size_t kMaximumDetailSegments = 64;

		struct SMeasuredSegment
		{
			WorldPt start;
			WorldPt end;
			double length = 0.0;
		};

		struct SCollection
		{
			std::vector<WorldPt> points;
			std::vector<SMeasuredSegment> segments;
			std::vector<SMeasuredSegment> detailSegments;
			vwad::algo::SpatialPointIndex pointIndex{ kTolerance };
			std::unordered_set<std::uint64_t> segmentKeys;
			std::unordered_set<std::uint64_t> detailSegmentKeys;
			std::vector<VWFC::Math::VWTransformMatrix> transforms;
			std::vector<MCObjectHandle> activeContainers;
			size_t visitedObjectCount = 0;
			bool sourceIsOpenPath = false;
			bool truncated = false;
		};

		struct SDominantAxis
		{
			WorldPt direction;
			SMeasuredSegment referenceSegment;
			double foldedAngleDegrees = 0.0;
			double supportingLength = 0.0;
			double confidence = 0.0;
			bool valid = false;
		};

		struct SAxisAlignedBounds
		{
			double minX = 0.0;
			double minY = 0.0;
			double maxX = 0.0;
			double maxY = 0.0;
			double width = 0.0;
			double height = 0.0;
			bool valid = false;
		};

		struct SOrientedBounds
		{
			WorldPt widthStart;
			WorldPt widthEnd;
			WorldPt heightStart;
			WorldPt heightEnd;
			double width = 0.0;
			double height = 0.0;
			bool valid = false;
		};

		static WorldPt TransformPoint(const WorldPt& point, const SCollection& collection)
		{
			VWFC::Math::VWPoint3D transformed(point.x, point.y, 0.0);
			for (auto it = collection.transforms.rbegin(); it != collection.transforms.rend(); ++it) {
				transformed = it->PointTransform(transformed);
			}
			return WorldPt(transformed.x, transformed.y);
		}

		static WorldPt TransformPoint(const WorldPt3& point, const SCollection& collection)
		{
			VWFC::Math::VWPoint3D transformed(point.x, point.y, point.z);
			for (auto it = collection.transforms.rbegin(); it != collection.transforms.rend(); ++it) {
				transformed = it->PointTransform(transformed);
			}
			return WorldPt(transformed.x, transformed.y);
		}

		static size_t AddWorldPoint(const WorldPt& point, SCollection& collection)
		{
			const size_t existing = collection.pointIndex.find({point.x, point.y});
			if (existing != std::numeric_limits<size_t>::max()) return existing;
			if (collection.points.size() >= kMaximumPoints) {
				collection.truncated = true;
				return std::numeric_limits<size_t>::max();
			}
			const size_t previousCount = collection.pointIndex.points().size();
			const size_t index = collection.pointIndex.insertOrFind({point.x, point.y});
			if (collection.pointIndex.points().size() == previousCount) return index;
			collection.points.push_back(point);
			return index;
		}

		static void AddWorldSegment(const WorldPt& start, const WorldPt& end, bool detail, SCollection& collection)
		{
			const size_t startIndex = AddWorldPoint(start, collection);
			const size_t endIndex = AddWorldPoint(end, collection);
			if (startIndex == std::numeric_limits<size_t>::max() ||
				endIndex == std::numeric_limits<size_t>::max() || startIndex == endIndex) return;

			SMeasuredSegment segment;
			segment.start = start;
			segment.end = end;
			segment.length = std::hypot(end.x - start.x, end.y - start.y);
			if (segment.length <= kTolerance) {
				return;
			}

			const std::uint64_t low = static_cast<std::uint64_t>(std::min(startIndex, endIndex));
			const std::uint64_t high = static_cast<std::uint64_t>(std::max(startIndex, endIndex));
			const std::uint64_t key = (low << 32) | high;
			if (collection.segmentKeys.insert(key).second) {
				if (collection.segments.size() < kMaximumSegments) {
					collection.segments.push_back(segment);
				}
				else {
					collection.truncated = true;
				}
			}

			if (detail && collection.detailSegmentKeys.insert(key).second) {
				if (collection.detailSegments.size() < kMaximumDetailSegments) {
					collection.detailSegments.push_back(segment);
				}
				else {
					collection.truncated = true;
				}
			}
		}

		static void AddWorldDetailSegment(const WorldPt& start, const WorldPt& end, SCollection& collection)
		{
			const size_t startIndex = AddWorldPoint(start, collection);
			const size_t endIndex = AddWorldPoint(end, collection);
			if (startIndex == std::numeric_limits<size_t>::max() ||
				endIndex == std::numeric_limits<size_t>::max() || startIndex == endIndex) return;
			SMeasuredSegment segment;
			segment.start = start;
			segment.end = end;
			segment.length = std::hypot(end.x - start.x, end.y - start.y);
			if (segment.length <= kTolerance) return;
			const std::uint64_t low = static_cast<std::uint64_t>(std::min(startIndex, endIndex));
			const std::uint64_t high = static_cast<std::uint64_t>(std::max(startIndex, endIndex));
			const std::uint64_t key = (low << 32) | high;
			if (!collection.detailSegmentKeys.insert(key).second) return;
			if (collection.detailSegments.size() < kMaximumDetailSegments) collection.detailSegments.push_back(segment);
			else collection.truncated = true;
		}

		static void AddLocalSegment(const WorldPt& start, const WorldPt& end, bool detail, SCollection& collection)
		{
			AddWorldSegment(TransformPoint(start, collection), TransformPoint(end, collection), detail, collection);
		}

		static void AddLocalDetailSegment(const WorldPt& start, const WorldPt& end, SCollection& collection)
		{
			AddWorldDetailSegment(TransformPoint(start, collection), TransformPoint(end, collection), collection);
		}

		struct SPolyEdgeContext
		{
			SCollection* collection = nullptr;
			bool collectDetail = false;
		};

		static void CollectPolyEdge(
			const WorldPt& start,
			const WorldPt& control,
			const WorldPt& end,
			WorldCoord radius,
			VertexType type,
			Sint8 visible,
			CallBackPtr callback,
			void* environment)
		{
			(void) callback;
			SPolyEdgeContext* context = static_cast<SPolyEdgeContext*>(environment);
			if (!context || !context->collection) {
				return;
			}

			AddWorldPoint(TransformPoint(start, *context->collection), *context->collection);
			AddWorldPoint(TransformPoint(end, *context->collection), *context->collection);
			if (visible == 0) return;
			if (type == vtCorner) {
				AddLocalSegment(start, end, context->collectDetail, *context->collection);
				return;
			}

			// ForEachPolyEdge supplies one control point for Bezier edges and a
			// signed radius for circular edges. Tessellate curved geometry for
			// bounds/intersections, while continuous annotation receives one
			// endpoint chord instead of dozens of tiny dimensions.
			if ((type == vtArc || type == vtRadius) && std::abs(radius) > kTolerance) {
				const WorldCoord dx = end.x - start.x;
				const WorldCoord dy = end.y - start.y;
				const WorldCoord chord = std::hypot(dx, dy);
				const WorldCoord absRadius = std::abs(radius);
				if (chord > kTolerance && absRadius + kTolerance >= chord * 0.5) {
					const WorldCoord midX = (start.x + end.x) * 0.5;
					const WorldCoord midY = (start.y + end.y) * 0.5;
					const WorldCoord centerDistance = std::sqrt(std::max<WorldCoord>(0.0, absRadius * absRadius - chord * chord * 0.25));
					const WorldCoord perpX = -dy / chord;
					const WorldCoord perpY = dx / chord;
					const WorldPt centers[2] = {
						WorldPt(midX + perpX * centerDistance, midY + perpY * centerDistance),
						WorldPt(midX - perpX * centerDistance, midY - perpY * centerDistance),
					};
					const double firstDistance = std::hypot(centers[0].x - control.x, centers[0].y - control.y);
					const double secondDistance = std::hypot(centers[1].x - control.x, centers[1].y - control.y);
					const WorldPt center = firstDistance <= secondDistance ? centers[0] : centers[1];
					double startAngle = std::atan2(start.y - center.y, start.x - center.x);
					double endAngle = std::atan2(end.y - center.y, end.x - center.x);
					double sweep = endAngle - startAngle;
					while (sweep < 0.0) sweep += 2.0 * kPi;
					const size_t steps = std::clamp<size_t>(static_cast<size_t>(std::ceil(std::abs(sweep) / (kPi / 18.0))), 2, 36);
					WorldPt previous = start;
					for (size_t index = 1; index <= steps; ++index) {
						const double angle = startAngle + sweep * static_cast<double>(index) / static_cast<double>(steps);
						const WorldPt current = index == steps ? end : WorldPt(center.x + absRadius * std::cos(angle), center.y + absRadius * std::sin(angle));
						AddLocalSegment(previous, current, false, *context->collection);
						previous = current;
					}
					if (context->collectDetail) AddLocalDetailSegment(start, end, *context->collection);
					return;
				}
			}

			const size_t steps = 16;
			WorldPt previous = start;
			for (size_t index = 1; index <= steps; ++index) {
				const double t = static_cast<double>(index) / static_cast<double>(steps);
				const double oneMinusT = 1.0 - t;
				const WorldPt current = index == steps ? end : WorldPt(
					oneMinusT * oneMinusT * start.x + 2.0 * oneMinusT * t * control.x + t * t * end.x,
					oneMinusT * oneMinusT * start.y + 2.0 * oneMinusT * t * control.y + t * t * end.y);
				AddLocalSegment(previous, current, false, *context->collection);
				previous = current;
			}
			if (context->collectDetail) AddLocalDetailSegment(start, end, *context->collection);
		}

		static void CollectPlanBounds(MCObjectHandle object, SCollection& collection)
		{
			WorldRectVerts bounds;
			if (!gSDK->GetObjectTopPlanBounds(object, bounds) || bounds.IsEmpty()) {
				return;
			}

			for (size_t index = 0; index < 4; ++index) {
				const WorldPt& start = bounds.ClockwiseFromTopLeft(index);
				const WorldPt& end = bounds.ClockwiseFromTopLeft((index + 1) % 4);
				AddLocalSegment(start, end, false, collection);
			}
		}

		static void Collect3DPolygon(MCObjectHandle object, SCollection& collection)
		{
			const short vertexCount = gSDK->CountVertices(object);
			if (vertexCount <= 0) {
				return;
			}

			std::vector<WorldPt> vertices;
			vertices.reserve(static_cast<size_t>(vertexCount));
			for (short index = 1; index <= vertexCount; ++index) {
				if (vertices.size() >= kMaximumPoints) {
					collection.truncated = true;
					break;
				}
				WorldPt3 point;
				gSDK->Get3DVertex(object, index, point);
				vertices.push_back(TransformPoint(point, collection));
			}

			for (size_t index = 1; index < vertices.size(); ++index) {
				AddWorldSegment(vertices[index - 1], vertices[index], false, collection);
			}
			if (vertices.size() > 2 && gSDK->GetPolyShapeClose(object)) {
				AddWorldSegment(vertices.back(), vertices.front(), false, collection);
			}
		}

		static bool IsContainerActive(MCObjectHandle container, const SCollection& collection)
		{
			return std::find(collection.activeContainers.begin(), collection.activeContainers.end(), container)
				!= collection.activeContainers.end();
		}

		static void TraverseObject(MCObjectHandle object, size_t depth, SCollection& collection);

		static void TraverseMembers(MCObjectHandle container, size_t depth, SCollection& collection)
		{
			if (!container || IsContainerActive(container, collection)) {
				return;
			}
			collection.activeContainers.push_back(container);
			for (MCObjectHandle member = gSDK->FirstMemberObj(container); member; member = gSDK->NextObject(member)) {
				TraverseObject(member, depth, collection);
				if (collection.visitedObjectCount >= kMaximumObjects) {
					collection.truncated = true;
					break;
				}
			}
			collection.activeContainers.pop_back();
		}

		static void PushObjectTransform(MCObjectHandle object, SCollection& collection)
		{
			TransformMatrix matrix;
			gSDK->GetEntityMatrix(object, matrix);
			collection.transforms.emplace_back(matrix);
		}

		static void TraverseObject(MCObjectHandle object, size_t depth, SCollection& collection)
		{
			if (!object) {
				return;
			}
			if (depth > kMaximumDepth || collection.visitedObjectCount >= kMaximumObjects) {
				collection.truncated = true;
				return;
			}
			++collection.visitedObjectCount;

			const short objectType = gSDK->GetObjectTypeN(object);
			const size_t pointCountBefore = collection.points.size();
			switch (objectType) {
				case kLineNode:
				{
					WorldPt start;
					WorldPt end;
					gSDK->GetEndPoints(object, start, end);
					AddLocalSegment(start, end, false, collection);
					break;
				}

				case kPolygonNode:
				case kPolylineNode:
				case kFreehandPolygonNode:
				{
					SPolyEdgeContext context;
					context.collection = &collection;
					context.collectDetail = depth == 0;
					gSDK->ForEachPolyEdge(object, CollectPolyEdge, &context);
					break;
				}

				case qPolyNode:
					Collect3DPolygon(object, collection);
					break;

				case kBoxNode:
				case rBoxNode:
				case kOvalNode:
				case kArcNode:
				case kWallNode:
				case kSlabNode:
					CollectPlanBounds(object, collection);
					break;

				case kGroupNode:
				case kSymDefNode:
					TraverseMembers(object, depth + 1, collection);
					break;

				case kSymbolNode:
				{
					MCObjectHandle definition = gSDK->GetDefinition(object);
					if (definition) {
						PushObjectTransform(object, collection);
						TraverseMembers(definition, depth + 1, collection);
						collection.transforms.pop_back();
					}
					break;
				}

				case kParametricNode:
				case kExtrudeNode:
				case kMultiExtrudeNode:
				case kSweepNode:
				{
					PushObjectTransform(object, collection);
					TraverseMembers(object, depth + 1, collection);
					collection.transforms.pop_back();
					break;
				}

				default:
					break;
			}

			if (collection.points.size() == pointCountBefore &&
				(objectType == kGroupNode || objectType == kSymbolNode || objectType == kParametricNode ||
				 objectType == kExtrudeNode || objectType == kMultiExtrudeNode || objectType == kSweepNode)) {
				CollectPlanBounds(object, collection);
			}
		}

		static SCollection Collect(MCObjectHandle sourceObject)
		{
			SCollection collection;
			if (!sourceObject) {
				return collection;
			}

			const short sourceType = gSDK->GetObjectTypeN(sourceObject);
			collection.sourceIsOpenPath =
				(sourceType == kPolygonNode || sourceType == kPolylineNode || sourceType == kFreehandPolygonNode) &&
				!gSDK->GetPolyShapeClose(sourceObject);
			TraverseObject(sourceObject, 0, collection);
			return collection;
		}

		static double FoldedAxisAngle(const SMeasuredSegment& segment)
		{
			double angle = std::atan2(segment.end.y - segment.start.y, segment.end.x - segment.start.x);
			while (angle < 0.0) angle += kPi;
			while (angle >= kPi) angle -= kPi;
			const double quarterTurn = kPi * 0.5;
			const double modulo = std::fmod(angle, quarterTurn);
			return std::min(modulo, quarterTurn - modulo);
		}

		static bool FindDominantAxis(const SCollection& collection, SDominantAxis& outAxis)
		{
			std::vector<vwad::algo::Segment2> segments;
			segments.reserve(collection.segments.size());
			for (const SMeasuredSegment& segment : collection.segments) {
				segments.push_back({
					{segment.start.x, segment.start.y},
					{segment.end.x, segment.end.y},
				});
			}
			const vwad::algo::DominantAxis axis = vwad::algo::findDominantAxis(segments);
			if (!axis.valid) return false;
			outAxis.direction = WorldPt(axis.direction.x, axis.direction.y);
			outAxis.referenceSegment.start = WorldPt(axis.reference.start.x, axis.reference.start.y);
			outAxis.referenceSegment.end = WorldPt(axis.reference.end.x, axis.reference.end.y);
			outAxis.referenceSegment.length = std::hypot(
				axis.reference.end.x - axis.reference.start.x,
				axis.reference.end.y - axis.reference.start.y);
			outAxis.foldedAngleDegrees = axis.foldedAngleDegrees;
			outAxis.supportingLength = axis.supportingLength;
			outAxis.confidence = axis.confidence;
			outAxis.valid = true;
			return true;
		}

		static bool CalculateAxisAlignedBounds(const SCollection& collection, SAxisAlignedBounds& outBounds)
		{
			if (collection.points.empty()) {
				return false;
			}

			outBounds.minX = std::numeric_limits<double>::max();
			outBounds.minY = std::numeric_limits<double>::max();
			outBounds.maxX = std::numeric_limits<double>::lowest();
			outBounds.maxY = std::numeric_limits<double>::lowest();
			for (const WorldPt& point : collection.points) {
				outBounds.minX = std::min(outBounds.minX, point.x);
				outBounds.minY = std::min(outBounds.minY, point.y);
				outBounds.maxX = std::max(outBounds.maxX, point.x);
				outBounds.maxY = std::max(outBounds.maxY, point.y);
			}

			outBounds.width = outBounds.maxX - outBounds.minX;
			outBounds.height = outBounds.maxY - outBounds.minY;
			outBounds.valid = outBounds.width > kTolerance || outBounds.height > kTolerance;
			return outBounds.valid;
		}

		static bool CalculateOrientedBounds(const SCollection& collection, SDominantAxis& axis, SOrientedBounds& outBounds)
		{
			if (!axis.valid || collection.points.empty()) {
				return false;
			}
			std::vector<vwad::algo::Point2> points;
			points.reserve(collection.points.size());
			for (const WorldPt& point : collection.points) {
				points.push_back({point.x, point.y});
			}
			vwad::algo::OrientedBounds bounds = vwad::algo::minimumAreaBounds(points);
			if (!bounds.valid) return false;
			// Keep the width axis aligned with the statistically dominant family. The
			// minimum-area rectangle may report either perpendicular edge first.
			const double alongWidth = std::abs(bounds.axis.x * axis.direction.x + bounds.axis.y * axis.direction.y);
			const double alongHeight = std::abs(bounds.normal.x * axis.direction.x + bounds.normal.y * axis.direction.y);
			if (alongHeight > alongWidth) {
				std::swap(bounds.width, bounds.height);
				std::swap(bounds.widthStart, bounds.heightStart);
				std::swap(bounds.widthEnd, bounds.heightEnd);
				bounds.axis = bounds.normal;
				bounds.normal = vwad::algo::Point2(-bounds.axis.y, bounds.axis.x);
			}
			if (bounds.axis.x < -kTolerance || (std::abs(bounds.axis.x) <= kTolerance && bounds.axis.y < 0.0)) {
				bounds.axis = bounds.axis * -1.0;
			}
			axis.direction = WorldPt(bounds.axis.x, bounds.axis.y);
			outBounds.width = bounds.width;
			outBounds.height = bounds.height;
			outBounds.widthStart = WorldPt(bounds.widthStart.x, bounds.widthStart.y);
			outBounds.widthEnd = WorldPt(bounds.widthEnd.x, bounds.widthEnd.y);
			outBounds.heightStart = WorldPt(bounds.heightStart.x, bounds.heightStart.y);
			outBounds.heightEnd = WorldPt(bounds.heightEnd.x, bounds.heightEnd.y);
			axis.referenceSegment.start = outBounds.widthStart;
			axis.referenceSegment.end = outBounds.widthEnd;
			axis.referenceSegment.length = outBounds.width;
			const double foldedAngle = std::atan2(std::abs(bounds.axis.y), std::abs(bounds.axis.x));
			axis.foldedAngleDegrees = std::min(foldedAngle, std::abs(kPi * 0.5 - foldedAngle)) * 180.0 / kPi;
			outBounds.valid = true;
			return outBounds.valid;
		}
	}
}
