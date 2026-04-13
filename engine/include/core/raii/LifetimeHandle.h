#pragma once
#include <core/raii/ILifetimeOwner.h>
#include <cstdint>
#include <cstring>
#include <type_traits>

//=============================================================================
// LifetimeHandle<T, KeyT>
//
// Generic RAII handle that pairs an ILifetimeOwner with a typed token. T names
// the value type on the far side of the handle; KeyT names the token shape used
// by the owner. Construction calls Attach(); destruction calls Detach().
//
// KeyT requirements:
//   - Pointer tokens (T*): encoded into ILifetimeOwner's uint64_t slot.
//     Validity check: Token != nullptr.
//   - Value tokens (e.g., DataBatchKey): copied into the uint64_t slot.
//     Validity check: Token != KeyT{}.
//
// Move-only. Designed for extensible use across any subsystem that
// implements ILifetimeOwner (batches, pools, registries, etc.).
//
// Common aliases:
//   RefBatchHandle<T>  = LifetimeHandle<T, T*>
//   DataBatchHandle<T> = LifetimeHandle<T, DataBatchKey>
//=============================================================================
template<typename T, typename KeyT>
class LifetimeHandle
{
	static_assert(sizeof(KeyT) <= sizeof(uint64_t),
		"LifetimeHandle key tokens must fit in ILifetimeOwner's uint64_t slot.");

public:
	LifetimeHandle() = default;

	// Standard construction: calls Attach on the owner.
	LifetimeHandle(ILifetimeOwner* owner, KeyT token)
		: Owner(owner)
		, Token(token)
	{
		if (Owner && IsTokenValid())
		{
			Owner->Attach(Encode());
		}
	}

	~LifetimeHandle()
	{
		Reset();
	}

	// Move-only semantics
	LifetimeHandle(LifetimeHandle&& other) noexcept
		: Owner(other.Owner)
		, Token(other.Token)
	{
		other.Owner = nullptr;
		other.Token = KeyT{};
	}

	LifetimeHandle& operator=(LifetimeHandle&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			Owner = other.Owner;
			Token = other.Token;
			other.Owner = nullptr;
			other.Token = KeyT{};
		}
		return *this;
	}

	LifetimeHandle(const LifetimeHandle&) = delete;
	LifetimeHandle& operator=(const LifetimeHandle&) = delete;

	// Manually release (calls Detach)
	void Reset()
	{
		if (Owner && IsTokenValid())
		{
			Owner->Detach(Encode());
		}
		Owner = nullptr;
		Token = KeyT{};
	}

	// Check validity
	bool IsValid() const { return Owner != nullptr && IsTokenValid(); }
	explicit operator bool() const { return IsValid(); }

	// Typed token access.
	KeyT GetToken() const { return Token; }

	// Owner access (for advanced use)
	ILifetimeOwner* GetOwner() const { return Owner; }

protected:
	// For friends that need to create handles without triggering Attach
	// (e.g., DataBatch::Emplace already added the item).
	struct NoAttachTag {};
	static constexpr NoAttachTag NoAttach{};

	LifetimeHandle(ILifetimeOwner* owner, KeyT token, NoAttachTag)
		: Owner(owner)
		, Token(token)
	{
	}

	template<typename U> friend class DataBatch;

private:
	uint64_t Encode() const
	{
		if constexpr (std::is_pointer_v<KeyT>)
		{
			return reinterpret_cast<uint64_t>(Token);
		}
		else
		{
			uint64_t v = 0;
			std::memcpy(&v, &Token, sizeof(KeyT));
			return v;
		}
	}

	bool IsTokenValid() const
	{
		if constexpr (std::is_pointer_v<KeyT>)
			return Token != nullptr;
		else
			return !(Token == KeyT{});
	}

	ILifetimeOwner* Owner = nullptr;
	KeyT Token{};
};
