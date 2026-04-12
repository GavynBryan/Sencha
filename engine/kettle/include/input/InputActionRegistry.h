#pragma once

#include <input/InputTypes.h>
#include <optional>
#include <span>
#include <string_view>

//=============================================================================
// InputActionRegistry
//
// Cold-path mapping from authored action names to app-owned, stable
// InputActionIds. Gameplay should compare InputActionIds directly; names are
// only for loading, validation, and diagnostics.
//=============================================================================

struct InputActionEntry
{
	std::string_view Name;
	InputActionId Id;
};

class IInputActionResolver
{
public:
	virtual ~IInputActionResolver() = default;

	[[nodiscard]] virtual std::optional<InputActionId> ResolveAction(
		std::string_view action) const = 0;

	[[nodiscard]] virtual std::span<const InputActionEntry> GetActions() const = 0;
};

class InputActionRegistry final : public IInputActionResolver
{
public:
	explicit InputActionRegistry(std::span<const InputActionEntry> actions)
		: Actions(actions)
	{
	}

	[[nodiscard]] std::optional<InputActionId> ResolveAction(
		std::string_view action) const override
	{
		for (const auto& entry : Actions)
		{
			if (entry.Name == action) return entry.Id;
		}

		return std::nullopt;
	}

	[[nodiscard]] std::span<const InputActionEntry> GetActions() const override
	{
		return Actions;
	}

private:
	std::span<const InputActionEntry> Actions;
};
