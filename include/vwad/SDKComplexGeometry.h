#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

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

		static bool ArePointsEqual(const WorldPt& a, const WorldPt& b)
		{
			const double dx = a.x - b.x;
			const double dy = a.y - b.y;
			return dx * dx + dy * dy <= kTolerance * kTolerance;
		}

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

		static void AddWorldPoint(const WorldPt& point, SCollection& collection)
		{
			for (const WorldPt& existing : collection.points) {
				if (ArePointsEqual(existing, point)) {
					return;
				}
			}

			if (collection.points.size() >= kMaximumPoints) {
				collection.truncated = true;
				return;
			}
			collection.points.push_back(point);
		}

		static bool ContainsSegment(const std::vector<SMeasuredSegment>& segments, const SMeasuredSegment& candidate)
		{
			for (const SMeasuredSegment& existing : segments) {
				const bool sameDirection = ArePointsEqual(existing.start, candidate.start) && ArePointsEqual(existing.end, candidate.end);
				const bool reverseDirection = ArePointsEqual(existing.start, candidate.end) && ArePointsEqual(existing.end, candidate.start);
				if (sameDirection || reverseDirection) {
					return true;
				}
			}
			return false;
		}

		static void AddWorldSegment(const WorldPt& start, const WorldPt& end, bool detail, SCollection& collection)
		{
			AddWorldPoint(start, collection);
			AddWorldPoint(end, collection);

			SMeasuredSegment segment;
			segment.start = start;
			segment.end = end;
			segment.length = std::hypot(end.x - start.x, end.y - start.y);
			if (segment.length <= kTolerance) {
				return;
			}

			if (!ContainsSegment(collection.segments, segment)) {
				if (collection.segments.size() < kMaximumSegments) {
					collection.segments.push_back(segment);
				}
				else {
					collection.truncated = true;
				}
			}

			if (detail && !ContainsSegment(collection.detailSegments, segment)) {
				if (collection.detailSegments.size() < kMaximumDetailSegments) {
					collection.detailSegments.push_back(segment);
				}
				else {
					collection.truncated = true;
				}
			}
		}

		static void AddLocalSegment(const WorldPt& start, const WorldPt& end, bool detail, SCollection& collection)
		{
			AddWorldSegment(TransformPoint(start, collection), TransformPoint(end, collection), detail, collection);
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
			(void) control;
			(void) radius;
			(void) callback;
			SPolyEdgeContext* context = static_cast<SPolyEdgeContext*>(environment);
			if (!context || !context->collection) {
				return;
			}

			AddWorldPoint(TransformPoint(start, *context->collection), *context->collection);
			AddWorldPoint(TransformPoint(end, *context->collection), *context->collection);
			if (visible != 0 && type == vtCorner) {
				AddLocalSegment(start, end, context->collectDetail, *context->collection);
			}
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
					context.collectDetail = collection.sourceIsOpenPath && depth == 0;
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
			if (collection.segments.empty()) {
				return false;
			}

			double maximumLength = 0.0;
			for (const SMeasuredSegment& segment : collection.segments) {
				maximumLength = std::max(maximumLength, segment.length);
			}
			if (maximumLength <= kTolerance) {
				return false;
			}

			static constexpr double kBinSizeDegrees = 5.0;
			std::array<double, 10> binLengths = {};
			const double minimumSupportingLength = maximumLength * 0.05;
			for (const SMeasuredSegment& segment : collection.segments) {
				if (segment.length < minimumSupportingLength) {
					continue;
				}
				const double foldedDegrees = FoldedAxisAngle(segment) * 180.0 / kPi;
				const size_t bin = std::min<size_t>(9, static_cast<size_t>(foldedDegrees / kBinSizeDegrees));
				binLengths[bin] += segment.length;
			}

			const size_t winningBin = static_cast<size_t>(
				std::distance(binLengths.begin(), std::max_element(binLengths.begin(), binLengths.end())));
			const SMeasuredSegment* reference = nullptr;
			for (const SMeasuredSegment& segment : collection.segments) {
				const double foldedDegrees = FoldedAxisAngle(segment) * 180.0 / kPi;
				const size_t bin = std::min<size_t>(9, static_cast<size_t>(foldedDegrees / kBinSizeDegrees));
				if (bin == winningBin && (!reference || segment.length > reference->length)) {
					reference = &segment;
				}
			}
			if (!reference) {
				return false;
			}

			double dx = reference->end.x - reference->start.x;
			double dy = reference->end.y - reference->start.y;
			if (dx < 0.0 || (std::abs(dx) <= kTolerance && dy < 0.0)) {
				dx = -dx;
				dy = -dy;
			}
			outAxis.direction = WorldPt(dx / reference->length, dy / reference->length);
			outAxis.referenceSegment = *reference;
			outAxis.foldedAngleDegrees = FoldedAxisAngle(*reference) * 180.0 / kPi;
			outAxis.supportingLength = binLengths[winningBin];
			outAxis.valid = true;
			return true;
		}

		static bool CalculateOrientedBounds(const SCollection& collection, const SDominantAxis& axis, SOrientedBounds& outBounds)
		{
			if (!axis.valid || collection.points.empty()) {
				return false;
			}

			const WorldPt normal(-axis.direction.y, axis.direction.x);
			double minimumU = std::numeric_limits<double>::max();
			double maximumU = std::numeric_limits<double>::lowest();
			double minimumV = std::numeric_limits<double>::max();
			double maximumV = std::numeric_limits<double>::lowest();
			for (const WorldPt& point : collection.points) {
				const double u = point.x * axis.direction.x + point.y * axis.direction.y;
				const double v = point.x * normal.x + point.y * normal.y;
				minimumU = std::min(minimumU, u);
				maximumU = std::max(maximumU, u);
				minimumV = std::min(minimumV, v);
				maximumV = std::max(maximumV, v);
			}

			outBounds.width = maximumU - minimumU;
			outBounds.height = maximumV - minimumV;
			outBounds.widthStart = WorldPt(
				axis.direction.x * minimumU + normal.x * minimumV,
				axis.direction.y * minimumU + normal.y * minimumV);
			outBounds.widthEnd = WorldPt(
				axis.direction.x * maximumU + normal.x * minimumV,
				axis.direction.y * maximumU + normal.y * minimumV);
			outBounds.heightStart = outBounds.widthEnd;
			outBounds.heightEnd = WorldPt(
				axis.direction.x * maximumU + normal.x * maximumV,
				axis.direction.y * maximumU + normal.y * maximumV);
			outBounds.valid = outBounds.width > kTolerance || outBounds.height > kTolerance;
			return outBounds.valid;
		}
	}
}
