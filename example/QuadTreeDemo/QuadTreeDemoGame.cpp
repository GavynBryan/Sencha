#include "QuadTreeDemoGame.h"

#include <algorithm>

namespace
{
	Aabb2d MakeCenteredAabb(const Vec2d& center, const Vec2d& size)
	{
		const Vec2d half = size * 0.5f;
		return Aabb2d(center - half, center + half);
	}

	void SubmitBox(SpriteFeature& sprites, BindlessImageIndex texture, const Aabb2d& bounds,
		uint32_t color, int32_t sortKey)
	{
		SpriteFeature::Sprite sprite{};
		sprite.CenterX = bounds.Center().X;
		sprite.CenterY = bounds.Center().Y;
		sprite.Width = bounds.Size().X;
		sprite.Height = bounds.Size().Y;
		sprite.Texture = texture;
		sprite.Color = color;
		sprite.SortKey = sortKey;
		sprites.Submit(sprite);
	}

	void SubmitLine(SpriteFeature& sprites, BindlessImageIndex texture,
		float centerX, float centerY, float width, float height, uint32_t color)
	{
		SpriteFeature::Sprite sprite{};
		sprite.CenterX = centerX;
		sprite.CenterY = centerY;
		sprite.Width = width;
		sprite.Height = height;
		sprite.Texture = texture;
		sprite.Color = color;
		sprite.SortKey = 0;
		sprites.Submit(sprite);
	}

	void SubmitOutline(SpriteFeature& sprites, BindlessImageIndex texture,
		const Aabb2d& bounds, uint32_t color)
	{
		constexpr float thickness = 1.0f;
		const float width = std::max(1.0f, bounds.Size().X);
		const float height = std::max(1.0f, bounds.Size().Y);
		const float centerX = bounds.Center().X;
		const float centerY = bounds.Center().Y;

		SubmitLine(sprites, texture, centerX, bounds.Min.Y, width, thickness, color);
		SubmitLine(sprites, texture, centerX, bounds.Max.Y, width, thickness, color);
		SubmitLine(sprites, texture, bounds.Min.X, centerY, thickness, height, color);
		SubmitLine(sprites, texture, bounds.Max.X, centerY, thickness, height, color);
	}
}

QuadTreeDemoPlayer::QuadTreeDemoPlayer(World2d& world, const Vec2d& position, const Vec2d& size)
	: Transform(world.Domain, Transform2f(position, 0.0f, Vec2d::One()))
	, Size(size)
{
}

QuadTreeDemoState::QuadTreeDemoState(World2d& world)
	: Tree(MakeTreeConfig())
	, Player(world, Vec2d(240.0f, 140.0f), Vec2d(32.0f, 32.0f))
{
}

QuadTree<uint32_t>::Config QuadTreeDemoState::MakeTreeConfig()
{
	QuadTree<uint32_t>::Config config;
	config.RootBounds = Aabb2d(Vec2d(0.0f, 0.0f), Vec2d(1280.0f, 720.0f));
	config.MaxDepth = 6;
	config.MaxEntriesPerLeaf = 0;
	return config;
}

QuadTreePlayerMovementSystem::QuadTreePlayerMovementSystem(
	QuadTreeDemoState& state,
	World2d& world,
	const EventBuffer<InputActionEvent>& inputEvents)
	: State(state)
	, World(world)
	, InputEvents(inputEvents)
	, PreviousTime(std::chrono::steady_clock::now())
{
}

void QuadTreePlayerMovementSystem::Update(const FrameTime& /*time*/)
{
	Vec2d direction = Vec2d::Zero();
	for (const auto& event : InputEvents.Items())
	{
		if (event.Action == InputActionId{QuadTreeDemoActions::Quit}
			&& event.Phase == InputPhase::Started)
		{
			State.Running = false;
		}

		if (event.Phase != InputPhase::Started && event.Phase != InputPhase::Performed)
		{
			continue;
		}

		if (event.Action == InputActionId{QuadTreeDemoActions::MoveLeft}) direction.X -= 1.0f;
		if (event.Action == InputActionId{QuadTreeDemoActions::MoveRight}) direction.X += 1.0f;
		if (event.Action == InputActionId{QuadTreeDemoActions::MoveUp}) direction.Y += 1.0f;
		if (event.Action == InputActionId{QuadTreeDemoActions::MoveDown}) direction.Y -= 1.0f;
	}

	const auto now = std::chrono::steady_clock::now();
	const std::chrono::duration<float> elapsed = now - PreviousTime;
	PreviousTime = now;
	const float dt = std::min(elapsed.count(), 1.0f / 15.0f);

	auto* local = World.Transforms.TryGetLocalMutable(State.Player.Transform.TransformKey());
	if (local == nullptr)
	{
		return;
	}

	constexpr float speed = 300.0f;
	if (direction.SqrMagnitude() > 0.0f)
	{
		direction = direction.Normalized();
		local->Position += direction * (speed * dt);
	}

	const Vec2d half = State.Player.Size * 0.5f;
	const Aabb2d& worldBounds = State.Tree.GetConfig().RootBounds;
	local->Position.X = std::clamp(
		local->Position.X,
		worldBounds.Min.X + half.X,
		worldBounds.Max.X - half.X);
	local->Position.Y = std::clamp(
		local->Position.Y,
		worldBounds.Min.Y + half.Y,
		worldBounds.Max.Y - half.Y);
}

QuadTreeRenderSystem::QuadTreeRenderSystem(
	QuadTreeDemoState& state,
	World2d& world,
	SpriteFeature& sprites,
	BindlessImageIndex whiteTexture)
	: State(state)
	, World(world)
	, Sprites(sprites)
	, WhiteTexture(whiteTexture)
{
}

void QuadTreeRenderSystem::Update(const FrameTime& /*time*/)
{
	const auto* playerWorld = World.Transforms.TryGetWorld(State.Player.Transform.TransformKey());
	if (playerWorld == nullptr)
	{
		return;
	}

	const Aabb2d playerBounds = MakeCenteredAabb(playerWorld->Position, State.Player.Size);
	State.Tree.Clear();
	State.Player.TreeEntry = State.Tree.Insert(0, playerBounds);

	State.Tree.ForEachNode([&](const Aabb2d& bounds, int depth)
	{
		const uint32_t alpha = static_cast<uint32_t>(std::max(34, 96 - depth * 10));
		const uint32_t color = 0x00FFFFFFu | (alpha << 24);
		SubmitOutline(Sprites, WhiteTexture, bounds, color);
	});

	SubmitOutline(Sprites, WhiteTexture, State.Tree.GetEntryNodeBounds(State.Player.TreeEntry), 0xBFFFFFFFu);
	SubmitBox(Sprites, WhiteTexture, playerBounds, 0xFFFFFFFFu, 10);
}
