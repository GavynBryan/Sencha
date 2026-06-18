#pragma once
#include <core/handle/ILifetimeOwner.h>
#include <cstdint>
#include <cstring>
#include <type_traits>

//=============================================================================
// Owned<H>
//
// The engine's single owning reference: a move-only RAII wrapper that pairs an
// ILifetimeOwner with a typed token H and ref-counts it — Attach() on
// construction, Detach() (release) on destruction. It is the one expression of
// the owning-vs-observing distinction:
//
//   FooHandle        -> observing (cheap, may go stale)
//   Owned<FooHandle> -> owning   (ref-counted, releases on destruction)
//
// The owner is type-erased to ILifetimeOwner*, so Owned<H> needs only the
// token type. H requirements:
//   - Value tokens (Handle<Tag>, DataBatchKey): copied into the owner's
//     uint64_t slot. Validity: Token != H{}.
//   - Pointer tokens (T*): encoded into the slot. Validity: Token != nullptr.
//
// Move-only. Works with any subsystem implementing ILifetimeOwner.
//=============================================================================
template <typename H>
class Owned
{
	static_assert(sizeof(H) <= sizeof(uint64_t),
		"Owned token must fit in ILifetimeOwner's uint64_t slot.");

public:
	Owned() = default;

	// Standard construction: calls Attach on the owner.
	Owned(ILifetimeOwner* owner, H token)
		: Owner(owner)
		, Token(token)
	{
		if (Owner && IsTokenValid())
		{
			Owner->Attach(Encode());
		}
	}

	~Owned()
	{
		Reset();
	}

	// Move-only semantics
	Owned(Owned&& other) noexcept
		: Owner(other.Owner)
		, Token(other.Token)
	{
		other.Owner = nullptr;
		other.Token = H{};
	}

	Owned& operator=(Owned&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			Owner = other.Owner;
			Token = other.Token;
			other.Owner = nullptr;
			other.Token = H{};
		}
		return *this;
	}

	Owned(const Owned&) = delete;
	Owned& operator=(const Owned&) = delete;

	// Manually release (calls Detach)
	void Reset()
	{
		if (Owner && IsTokenValid())
		{
			Owner->Detach(Encode());
		}
		Owner = nullptr;
		Token = H{};
	}

	// Check validity
	bool IsValid() const { return Owner != nullptr && IsTokenValid(); }
	explicit operator bool() const { return IsValid(); }

	// Typed token access.
	H GetToken() const { return Token; }

	// Owner access (for advanced use)
	ILifetimeOwner* GetOwner() const { return Owner; }

	// Take ownership of a resource without calling Attach. Use this when the
	// caller has already incremented the refcount (e.g. just created the item)
	// and wants to wrap it in a RAII handle without double-counting. The tag
	// type makes the intent explicit at every call site.
	struct NoAttachTag {};
	static constexpr NoAttachTag NoAttach{};

	Owned(ILifetimeOwner* owner, H token, NoAttachTag)
		: Owner(owner)
		, Token(token)
	{
	}

protected:
	template<typename U> friend class DataBatch;

private:
	uint64_t Encode() const
	{
		if constexpr (std::is_pointer_v<H>)
		{
			return reinterpret_cast<uint64_t>(Token);
		}
		else
		{
			// Generic value-token encode: H may be Handle<Tag>, DataBatchKey, etc.
			// For Handle tokens this is byte-identical to Handle::ToToken() on
			// little-endian targets (the decode side uses Handle::FromToken()).
			uint64_t v = 0;
			std::memcpy(&v, &Token, sizeof(H));
			return v;
		}
	}

	bool IsTokenValid() const
	{
		if constexpr (std::is_pointer_v<H>)
			return Token != nullptr;
		else
			return !(Token == H{});
	}

	ILifetimeOwner* Owner = nullptr;
	H Token{};
};
