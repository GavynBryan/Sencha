#include <math/geometry/3d/AabbTransform.h>

Aabb3d TransformAabb(const Aabb3d& local, const Mat4& world)
{
	Aabb3d result = Aabb3d::Empty();
	for (int x = 0; x < 2; ++x)
	for (int y = 0; y < 2; ++y)
	for (int z = 0; z < 2; ++z)
	{
		const Vec3d corner(
			x == 0 ? local.Min.X : local.Max.X,
			y == 0 ? local.Min.Y : local.Max.Y,
			z == 0 ? local.Min.Z : local.Max.Z);
		result.ExpandToInclude(world.TransformPoint(corner));
	}
	return result;
}
