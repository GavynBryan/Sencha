#pragma once

#include <input/InputTypes.h>
#include <optional>
#include <span>
#include <string_view>

//=============================================================================
// InputActionRegistry
//
// Cold-path mapping from authored action names to app-owned, stable
// InputActionIds. IDs are assigned by position in the names array (0-based).
// Gameplay should compare InputActionIds directly; names are only for
// loading, validation, and diagnostics.
//=============================================================================

class IInputActionResolver
{
public:
	virtual ~IInputActionResolver() = default;

	[[nodiscard]] virtual std::optional<InputActionId> ResolveAction(
		std::string_view action) const = 0;

	[[nodiscard]] virtual std::span<const std::string_view> GetActionNames() const = 0;

	[[nodiscard]] virtual uint16_t GetActionCount() const = 0;
};

class InputActionRegistry final : public IInputActionResolver
{
public:
	explicit InputActionRegistry(std::span<const std::string_view> names)
		: Names(names)
	{
	}

	[[nodiscard]] std::optional<InputActionId> ResolveAction(
		std::string_view action) const override
	{
		for (uint16_t i = 0; i < Names.size(); ++i)
		{
			if (Names[i] == action) return InputActionId{i};
		}

		return std::nullopt;
	}

	[[nodiscard]] std::span<const std::string_view> GetActionNames() const override
	{
		return Names;
	}

	[[nodiscard]] uint16_t GetActionCount() const override
	{
		return static_cast<uint16_t>(Names.size());
	}

private:
	std::span<const std::string_view> Names;
};
