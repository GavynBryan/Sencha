#include <math/geometry/2d/Rect2d.h>

#include <ostream>

Rect2d::Rect2d(const Vec2d& position, const Vec2d& size)
	: Position(position), Size(size)
{
}

Rect2d::Rect2d(float x, float y, float width, float height)
	: Position(x, y), Size(width, height)
{
}

bool Rect2d::IsValid() const
{
	return Size.X >= 0.0f && Size.Y >= 0.0f;
}

Vec2d Rect2d::Min() const { return Position; }
Vec2d Rect2d::Max() const { return Position + Size; }
Vec2d Rect2d::Center() const { return Position + Size / 2.0f; }
float Rect2d::Width() const { return Size.X; }
float Rect2d::Height() const { return Size.Y; }
float Rect2d::Area() const { return Size.X * Size.Y; }

bool Rect2d::Contains(const Vec2d& point) const
{
	const Vec2d max = Max();
	return point.X >= Position.X && point.X <= max.X
		&& point.Y >= Position.Y && point.Y <= max.Y;
}

bool Rect2d::Intersects(const Rect2d& other) const
{
	const Vec2d maxA = Max();
	const Vec2d maxB = other.Max();
	return Position.X <= maxB.X && maxA.X >= other.Position.X
		&& Position.Y <= maxB.Y && maxA.Y >= other.Position.Y;
}

bool Rect2d::operator==(const Rect2d& other) const
{
	return Position == other.Position && Size == other.Size;
}

Rect2d Rect2d::FromMinMax(const Vec2d& min, const Vec2d& max)
{
	return Rect2d(min, max - min);
}

Rect2d Rect2d::FromCenterSize(const Vec2d& center, const Vec2d& size)
{
	return Rect2d(center - size / 2.0f, size);
}

std::ostream& operator<<(std::ostream& os, const Rect2d& rect)
{
	os << "{Position: " << rect.Position << ", Size: " << rect.Size << "}";
	return os;
}
