#pragma once

#include <cmath>
#include <limits>
#include <sstream>
#include <string>

namespace AutoDimensionPlugin
{
	namespace ViewPlane
	{
		static constexpr double kTolerance = 1e-6;

		enum class EPlaneKind
		{
			GroundPlan,
			Front,
			Back,
			Left,
			Right,
		};

		// A measuring plane for the active view. Top/Plan and every view that is not one
		// of the four standard elevations keeps the historical ground-plane behaviour.
		// The elevations measure the real Z range of the source instead of a top view
		// projection, so the dimensions are created on a working plane that faces the
		// camera.
		struct SViewPlane
		{
			EPlaneKind		kind = EPlaneKind::GroundPlan;
			TStandardView	standardView = standardViewTop;
			Axis			axis;
			Axis			savedWorkingPlane;
			TPlanarRefID	planarRefID = 0;
			bool			planar = false;
			bool			workingPlaneChanged = false;
			bool			planarRefFailed = false;
			const char*		name = "ground-plan";
		};

		struct SPlanarBounds
		{
			double	minU = std::numeric_limits<double>::max();
			double	minV = std::numeric_limits<double>::max();
			double	maxU = std::numeric_limits<double>::lowest();
			double	maxV = std::numeric_limits<double>::lowest();
			bool	valid = false;

			void Add(const WorldPt& planarPoint)
			{
				minU = std::min<double>(minU, planarPoint.x);
				maxU = std::max<double>(maxU, planarPoint.x);
				minV = std::min<double>(minV, planarPoint.y);
				maxV = std::max<double>(maxV, planarPoint.y);
				valid = true;
			}

			double Width() const { return valid ? maxU - minU : 0.0; }
			double Height() const { return valid ? maxV - minV : 0.0; }
		};

		static bool GetStandardPlaneAxes(TStandardView view, EPlaneKind& outKind, const char*& outName, Vector& outI, Vector& outJ, Vector& outK)
		{
			// i is screen right, j is screen up, k points at the camera.
			outJ = Vector(0.0, 0.0, 1.0);
			switch (view) {
				case standardViewFront:
					outKind = EPlaneKind::Front;
					outName = "front";
					outI = Vector(1.0, 0.0, 0.0);
					outK = Vector(0.0, -1.0, 0.0);
					return true;

				case standardViewBack:
					outKind = EPlaneKind::Back;
					outName = "back";
					outI = Vector(-1.0, 0.0, 0.0);
					outK = Vector(0.0, 1.0, 0.0);
					return true;

				case standardViewLeft:
					outKind = EPlaneKind::Left;
					outName = "left";
					outI = Vector(0.0, -1.0, 0.0);
					outK = Vector(-1.0, 0.0, 0.0);
					return true;

				case standardViewRight:
					outKind = EPlaneKind::Right;
					outName = "right";
					outI = Vector(0.0, 1.0, 0.0);
					outK = Vector(1.0, 0.0, 0.0);
					return true;

				default:
					return false;
			}
		}

		static WorldPt ProjectManually(const SViewPlane& plane, const WorldPt3& modelPoint)
		{
			const double dx = modelPoint.x - plane.axis.vertex.x;
			const double dy = modelPoint.y - plane.axis.vertex.y;
			const double dz = modelPoint.z - plane.axis.vertex.z;
			return WorldPt(
				dx * plane.axis.i.x + dy * plane.axis.i.y + dz * plane.axis.i.z,
				dx * plane.axis.j.x + dy * plane.axis.j.y + dz * plane.axis.j.z);
		}

		static WorldPt Project(const SViewPlane& plane, const WorldPt3& modelPoint)
		{
			if (!plane.planar) {
				return WorldPt(modelPoint.x, modelPoint.y);
			}

			WorldPt planarPoint;
			if (gSDK->ModelPtToPlanarPt(plane.planarRefID, modelPoint, planarPoint)) {
				return planarPoint;
			}
			return ProjectManually(plane, modelPoint);
		}

		static WorldPt3 GetCubeCenter(const WorldCube& cube)
		{
			return WorldPt3(
				(cube.MinX() + cube.MaxX()) * 0.5,
				(cube.MinY() + cube.MaxY()) * 0.5,
				(cube.MinZ() + cube.MaxZ()) * 0.5);
		}

		static void AddCubeCorners(const SViewPlane& plane, const WorldCube& cube, SPlanarBounds& ioBounds)
		{
			const double xs[2] = { cube.MinX(), cube.MaxX() };
			const double ys[2] = { cube.MinY(), cube.MaxY() };
			const double zs[2] = { cube.MinZ(), cube.MaxZ() };
			for (double x : xs) {
				for (double y : ys) {
					for (double z : zs) {
						ioBounds.Add(Project(plane, WorldPt3(x, y, z)));
					}
				}
			}
		}

		// Places the plane on the face of the reference cube that faces the camera so the
		// dimensions are not buried inside rendered geometry, then makes it the working
		// plane to obtain a planar reference for the created dimensions.
		static SViewPlane Begin(const WorldCube& referenceCube)
		{
			SViewPlane plane;
			plane.standardView = gSDK->GetCurrentView();

			Vector i;
			Vector j;
			Vector k;
			if (!GetStandardPlaneAxes(plane.standardView, plane.kind, plane.name, i, j, k)) {
				return plane;
			}

			const WorldPt3 center = GetCubeCenter(referenceCube);
			const double depth = std::abs(
				k.x * (referenceCube.MaxX() - referenceCube.MinX()) +
				k.y * (referenceCube.MaxY() - referenceCube.MinY()) +
				k.z * (referenceCube.MaxZ() - referenceCube.MinZ()));
			const double halfDepth = depth * 0.5;

			plane.axis.i = i;
			plane.axis.j = j;
			plane.axis.k = k;
			plane.axis.vertex = WorldPt3(
				center.x + k.x * halfDepth,
				center.y + k.y * halfDepth,
				center.z + k.z * halfDepth);

			gSDK->GetWorkingPlane(plane.savedWorkingPlane);
			gSDK->NewWorkingPlane(plane.axis);
			plane.workingPlaneChanged = true;
			plane.planarRefID = gSDK->GetWorkingPlanePlanarRefID();
			if (plane.planarRefID == 0) {
				// Without a planar reference the dimensions would land on the ground
				// plane and read as a degenerate line in the elevation. Restore the
				// working plane and keep the documented plan behaviour instead.
				gSDK->NewWorkingPlane(plane.savedWorkingPlane);
				plane.workingPlaneChanged = false;
				plane.kind = EPlaneKind::GroundPlan;
				plane.name = "ground-plan";
				plane.planarRefFailed = true;
				return plane;
			}

			plane.planar = true;
			return plane;
		}

		static void End(SViewPlane& plane)
		{
			if (plane.workingPlaneChanged) {
				gSDK->NewWorkingPlane(plane.savedWorkingPlane);
				plane.workingPlaneChanged = false;
			}
		}

		static void ApplyPlanarRef(MCObjectHandle object, const SViewPlane& plane)
		{
			if (object && plane.planar) {
				gSDK->SetPlanarRefID(object, plane.planarRefID);
			}
		}

		static std::string Describe(const SViewPlane& plane)
		{
			std::ostringstream output;
			output.precision(12);
			output << "view-plane name=" << plane.name
				<< " standardView=" << plane.standardView
				<< " planar=" << (plane.planar ? "true" : "false")
				<< " planarRefID=" << plane.planarRefID
				<< " planarRefFailed=" << (plane.planarRefFailed ? "true" : "false");
			if (plane.planar) {
				output << " vertex=(" << plane.axis.vertex.x << ',' << plane.axis.vertex.y << ',' << plane.axis.vertex.z << ')'
					<< " i=(" << plane.axis.i.x << ',' << plane.axis.i.y << ',' << plane.axis.i.z << ')'
					<< " j=(" << plane.axis.j.x << ',' << plane.axis.j.y << ',' << plane.axis.j.z << ')'
					<< " k=(" << plane.axis.k.x << ',' << plane.axis.k.y << ',' << plane.axis.k.z << ')';
			}
			return output.str();
		}
	}
}
