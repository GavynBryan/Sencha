#pragma once

#include <core/event/EventBuffer.h>
#include <core/service/IService.h>
#include <input/InputTypes.h>
#include <math/spatial/QuadTree.h>
#include <render/backend/vulkan/VulkanDescriptorCache.h>
#include <render/features/SpriteFeature.h>
#include <world/World.h>
#include <world/transform/TransformNode.h>

#include <cstdint>
#include <string_view>

namespace QuadTreeDemoActions
{
	inline constexpr std::string_view Names[] = {
		"MoveLeft",
		"MoveRight",
		"MoveUp",
		"MoveDown",
		"Quit",
	};

	enum Action : uint16_t
	{
		MoveLeft,
		MoveRight,
		MoveUp,
		MoveDown,
		Quit,
		Count,
	};
}

struct QuadTreeDemoPlayer
{
	QuadTreeDemoPlayer(World2d& world, const Vec2d& position, const Vec2d& size);

	TransformNode2d Transform;
	Vec2d Size;
	QuadTree<uint32_t>::EntryId TreeEntry = QuadTree<uint32_t>::InvalidEntry;
};

class QuadTreeDemoState final : public IService
{
public:
	explicit QuadTreeDemoState(World2d& world);

	static QuadTree<uint32_t>::Config MakeTreeConfig();

	bool Running = true;
	QuadTree<uint32_t> Tree;
	QuadTreeDemoPlayer Player;
};

class QuadTreePlayerMovementSystem
{
public:
	QuadTreePlayerMovementSystem(
		QuadTreeDemoState& state,
		World2d& world,
		const EventBuffer<InputActionEvent>& inputEvents);

	void Update(float dt);

private:
	QuadTreeDemoState& State;
	World2d& World;
	const EventBuffer<InputActionEvent>& InputEvents;
};

class QuadTreeRenderSystem
{
public:
	QuadTreeRenderSystem(
		QuadTreeDemoState& state,
		World2d& world,
		SpriteFeature& sprites,
		BindlessImageIndex whiteTexture);

	void Update(float dt);

private:
	QuadTreeDemoState& State;
	World2d& World;
	SpriteFeature& Sprites;
	BindlessImageIndex WhiteTexture;
};
