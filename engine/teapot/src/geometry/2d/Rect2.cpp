#include <geometry/2d/Rect2.h>

#include <ostream>

Rect2::Rect2(const Vec2& position, const Vec2& size)
	: Position(position), Size(size)
{
}

Rect2::Rect2(float x, float y, float width, float height)
	: Position(x, y), Size(width, height)
{
}

bool Rect2::IsValid() const
{
	return Size.X >= 0.0f && Size.Y >= 0.0f;
}

Vec2 Rect2::Min() const { return Position; }
Vec2 Rect2::Max() const { return Position + Size; }
Vec2 Rect2::Center() const { return Position + Size / 2.0f; }
float Rect2::Width() const { return Size.X; }
float Rect2::Height() const { return Size.Y; }
float Rect2::Area() const { return Size.X * Size.Y; }

bool Rect2::Contains(const Vec2& point) const
{
	const Vec2 max = Max();
	return point.X >= Position.X && point.X <= max.X
		&& point.Y >= Position.Y && point.Y <= max.Y;
}

bool Rect2::Intersects(const Rect2& other) const
{
	const Vec2 maxA = Max();
	const Vec2 maxB = other.Max();
	return Position.X <= maxB.X && maxA.X >= other.Position.X
		&& Position.Y <= maxB.Y && maxA.Y >= other.Position.Y;
}

bool Rect2::operator==(const Rect2& other) const
{
	return Position == other.Position && Size == other.Size;
}

Rect2 Rect2::FromMinMax(const Vec2& min, const Vec2& max)
{
	return Rect2(min, max - min);
}

Rect2 Rect2::FromCenterSize(const Vec2& center, const Vec2& size)
{
	return Rect2(center - size / 2.0f, size);
}

std::ostream& operator<<(std::ostream& os, const Rect2& rect)
{
	os << "{Position: " << rect.Position << ", Size: " << rect.Size << "}";
	return os;
}
