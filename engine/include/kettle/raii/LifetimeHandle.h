#pragma once
#include <kettle/raii/ILifetimeOwner.h>
#include <concepts>
#include <type_traits>

//=============================================================================
// LifetimeHandle<TokenT>
//
// Generic RAII handle that pairs an ILifetimeOwner with a typed token.
// Construction calls Attach(); destruction calls Detach(). The void*
// type-erasure required by ILifetimeOwner is hidden inside the handle —
// callers only ever interact with TokenT.
//
// TokenT requirements:
//   - Pointer types (T*):  passed through ILifetimeOwner as-is.
//     Validity check: Token != nullptr.
//   - Value types (e.g., DataBatchKey): passed by address (&Token).
//     Validity check: Token != TokenT{}.
//
// Move-only. Designed for extensible use across any subsystem that
// implements ILifetimeOwner (batches, pools, registries, etc.).
//
// Common aliases:
//   RefBatchHandle<T>  = LifetimeHandle<T*>
//   DataBatchHandle<T> = LifetimeHandle<DataBatchKey>
//=============================================================================
template<typename TokenT>
class LifetimeHandle
{
public:
	LifetimeHandle() = default;

	// Standard construction: calls Attach on the owner.
	LifetimeHandle(ILifetimeOwner* owner, TokenT token)
		: Owner(owner)
		, Token(token)
	{
		if (Owner && IsTokenValid())
		{
			Owner->Attach(Erase());
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
		other.Token = TokenT{};
	}

	LifetimeHandle& operator=(LifetimeHandle&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			Owner = other.Owner;
			Token = other.Token;
			other.Owner = nullptr;
			other.Token = TokenT{};
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
			Owner->Detach(Erase());
		}
		Owner = nullptr;
		Token = TokenT{};
	}

	// Check validity
	bool IsValid() const { return Owner != nullptr && IsTokenValid(); }
	explicit operator bool() const { return IsValid(); }

	// Typed token access — callers never see void*.
	TokenT GetToken() const { return Token; }

	// Owner access (for advanced use)
	ILifetimeOwner* GetOwner() const { return Owner; }

protected:
	// For friends that need to create handles without triggering Attach
	// (e.g., DataBatch::Emplace already added the item).
	struct NoAttachTag {};
	static constexpr NoAttachTag NoAttach{};

	LifetimeHandle(ILifetimeOwner* owner, TokenT token, NoAttachTag)
		: Owner(owner)
		, Token(token)
	{
	}

	template<typename T> friend class DataBatch;

private:
	// Type-erasure bridge: pointer tokens pass the pointer itself,
	// value tokens pass the address of the stored member.
	void* Erase()
	{
		if constexpr (std::is_pointer_v<TokenT>)
			return static_cast<void*>(Token);
		else
			return static_cast<void*>(&Token);
	}

	bool IsTokenValid() const
	{
		if constexpr (std::is_pointer_v<TokenT>)
			return Token != nullptr;
		else
			return !(Token == TokenT{});
	}

	ILifetimeOwner* Owner = nullptr;
	TokenT Token{};
};
