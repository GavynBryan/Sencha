#include <gtest/gtest.h>
#include <geometry/Frustum.h>
#include <cmath>

// Helper: build a simple symmetric perspective VP matrix using Mat4 factories.
static Mat4 MakeTestVP()
{
	// Looking down -Z from origin, standard perspective
	Mat4 view = Mat4::MakeLookAt(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 1.0f, 0.0f));
	float fov = 90.0f * (3.14159265358979f / 180.0f); // 90 degrees
	Mat4 proj = Mat4::MakePerspective(fov, 1.0f, 0.1f, 100.0f);
	return proj * view;
}

// --- Plane Extraction Sanity ---

TEST(Frustum, PlaneExtractioProducesNormalizedPlanes)
{
	Mat4 vp = MakeTestVP();
	Frustumf frustum = Frustumf::FromViewProjection(vp);

	for (int i = 0; i < Frustumf::PlaneCount; ++i)
	{
		float mag = frustum.Planes[i].Normal.Magnitude();
		EXPECT_NEAR(mag, 1.0f, 1e-5f) << "Plane " << i << " normal is not unit length";
	}
}

TEST(Frustum, NearAndFarPlanesOppose)
{
	Mat4 vp = MakeTestVP();
	Frustumf frustum = Frustumf::FromViewProjection(vp);

	// Near and far normals should point in roughly opposite directions
	float dot = frustum.Planes[Frustumf::Near].Normal.Dot(
		frustum.Planes[Frustumf::Far].Normal);
	EXPECT_LT(dot, 0.0f);
}

TEST(Frustum, LeftAndRightPlanesOppose)
{
	Mat4 vp = MakeTestVP();
	Frustumf frustum = Frustumf::FromViewProjection(vp);

	float dot = frustum.Planes[Frustumf::Left].Normal.Dot(
		frustum.Planes[Frustumf::Right].Normal);
	EXPECT_LE(dot, 0.0f);
}

TEST(Frustum, TopAndBottomPlanesOppose)
{
	Mat4 vp = MakeTestVP();
	Frustumf frustum = Frustumf::FromViewProjection(vp);

	float dot = frustum.Planes[Frustumf::Top].Normal.Dot(
		frustum.Planes[Frustumf::Bottom].Normal);
	EXPECT_LE(dot, 0.0f);
}

// --- Contains Point ---

TEST(Frustum, ContainsPointInsideFrustum)
{
	Mat4 vp = MakeTestVP();
	Frustumf frustum = Frustumf::FromViewProjection(vp);

	// Point directly ahead inside the frustum
	EXPECT_TRUE(frustum.ContainsPoint(Vec3(0.0f, 0.0f, -5.0f)));
	EXPECT_TRUE(frustum.ContainsPoint(Vec3(0.0f, 0.0f, -50.0f)));
}

TEST(Frustum, DoesNotContainPointBehindCamera)
{
	Mat4 vp = MakeTestVP();
	Frustumf frustum = Frustumf::FromViewProjection(vp);

	EXPECT_FALSE(frustum.ContainsPoint(Vec3(0.0f, 0.0f, 5.0f)));
}

TEST(Frustum, DoesNotContainPointBeyondFar)
{
	Mat4 vp = MakeTestVP();
	Frustumf frustum = Frustumf::FromViewProjection(vp);

	EXPECT_FALSE(frustum.ContainsPoint(Vec3(0.0f, 0.0f, -200.0f)));
}

TEST(Frustum, DoesNotContainPointFarToTheSide)
{
	Mat4 vp = MakeTestVP();
	Frustumf frustum = Frustumf::FromViewProjection(vp);

	// At z=-5 with 90-degree FOV and aspect 1, half-width ~= 5
	EXPECT_FALSE(frustum.ContainsPoint(Vec3(100.0f, 0.0f, -5.0f)));
}

// --- Intersects Sphere ---

TEST(Frustum, IntersectsSphereInside)
{
	Mat4 vp = MakeTestVP();
	Frustumf frustum = Frustumf::FromViewProjection(vp);

	Spheref sphere(Vec3(0.0f, 0.0f, -10.0f), 1.0f);
	EXPECT_TRUE(frustum.IntersectsSphere(sphere));
}

TEST(Frustum, IntersectsSphereCrossingBoundary)
{
	Mat4 vp = MakeTestVP();
	Frustumf frustum = Frustumf::FromViewProjection(vp);

	// Sphere centered behind camera but large enough to reach into frustum
	Spheref sphere(Vec3(0.0f, 0.0f, 0.5f), 5.0f);
	EXPECT_TRUE(frustum.IntersectsSphere(sphere));
}

TEST(Frustum, DoesNotIntersectSphereCompletelyOutside)
{
	Mat4 vp = MakeTestVP();
	Frustumf frustum = Frustumf::FromViewProjection(vp);

	Spheref sphere(Vec3(0.0f, 0.0f, 10.0f), 1.0f);
	EXPECT_FALSE(frustum.IntersectsSphere(sphere));
}

// --- Intersects AABB ---

TEST(Frustum, IntersectsAabbInside)
{
	Mat4 vp = MakeTestVP();
	Frustumf frustum = Frustumf::FromViewProjection(vp);

	Aabb3f box(Vec3(-1.0f, -1.0f, -11.0f), Vec3(1.0f, 1.0f, -9.0f));
	EXPECT_TRUE(frustum.IntersectsAabb(box));
}

TEST(Frustum, DoesNotIntersectAabbBehindCamera)
{
	Mat4 vp = MakeTestVP();
	Frustumf frustum = Frustumf::FromViewProjection(vp);

	Aabb3f box(Vec3(-1.0f, -1.0f, 5.0f), Vec3(1.0f, 1.0f, 10.0f));
	EXPECT_FALSE(frustum.IntersectsAabb(box));
}

TEST(Frustum, DoesNotIntersectAabbBeyondFar)
{
	Mat4 vp = MakeTestVP();
	Frustumf frustum = Frustumf::FromViewProjection(vp);

	Aabb3f box(Vec3(-1.0f, -1.0f, -200.0f), Vec3(1.0f, 1.0f, -150.0f));
	EXPECT_FALSE(frustum.IntersectsAabb(box));
}

TEST(Frustum, IntersectsAabbCrossingNearPlane)
{
	Mat4 vp = MakeTestVP();
	Frustumf frustum = Frustumf::FromViewProjection(vp);

	// Box straddling the near plane
	Aabb3f box(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f));
	EXPECT_TRUE(frustum.IntersectsAabb(box));
}

// --- Orthographic Frustum ---

TEST(Frustum, OrthographicFrustumContainsExpectedPoints)
{
	Mat4 view = Mat4::MakeLookAt(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 1.0f, 0.0f));
	Mat4 proj = Mat4::MakeOrthographic(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 100.0f);
	Mat4 vp = proj * view;
	Frustumf frustum = Frustumf::FromViewProjection(vp);

	EXPECT_TRUE(frustum.ContainsPoint(Vec3(0.0f, 0.0f, -50.0f)));
	EXPECT_TRUE(frustum.ContainsPoint(Vec3(9.0f, 9.0f, -1.0f)));
	EXPECT_FALSE(frustum.ContainsPoint(Vec3(11.0f, 0.0f, -50.0f)));
	EXPECT_FALSE(frustum.ContainsPoint(Vec3(0.0f, 0.0f, -101.0f)));
}
