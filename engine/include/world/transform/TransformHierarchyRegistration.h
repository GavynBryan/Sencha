#pragma once

#include <core/batch/DataBatchKey.h>
#include <world/transform/TransformHierarchyService.h>

//=============================================================================
// TransformHierarchyRegistration
//
// Move-only RAII guard that owns a single Register/Unregister pair on a
// TransformHierarchyService. Construction registers the key; destruction
// unregisters it. Gameplay types that participate in the spatial hierarchy
// hold one of these alongside their DataBatchHandle<TTransform> and get
// rule-of-zero lifetime management for free — no hand-rolled destructor,
// no move-ctor null-pointer dance.
//
// The key is NOT owned here; the paired DataBatchHandle owns the transform
// slot. This handle only owns the hierarchy-side registration.
//=============================================================================
class TransformHierarchyRegistration
{
public:
	TransformHierarchyRegistration() = default;

	TransformHierarchyRegistration(TransformHierarchyService& hierarchy, DataBatchKey key)
		: Hierarchy(&hierarchy)
		, Key(key)
	{
		if (Key.Value != 0)
			Hierarchy->Register(Key);
	}

	~TransformHierarchyRegistration()
	{
		Reset();
	}

	TransformHierarchyRegistration(TransformHierarchyRegistration&& other) noexcept
		: Hierarchy(other.Hierarchy)
		, Key(other.Key)
	{
		other.Hierarchy = nullptr;
		other.Key = DataBatchKey{};
	}

	TransformHierarchyRegistration& operator=(TransformHierarchyRegistration&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			Hierarchy = other.Hierarchy;
			Key = other.Key;
			other.Hierarchy = nullptr;
			other.Key = DataBatchKey{};
		}
		return *this;
	}

	TransformHierarchyRegistration(const TransformHierarchyRegistration&) = delete;
	TransformHierarchyRegistration& operator=(const TransformHierarchyRegistration&) = delete;

	void Reset()
	{
		if (Hierarchy && Key.Value != 0)
			Hierarchy->Unregister(Key);
		Hierarchy = nullptr;
		Key = DataBatchKey{};
	}

	bool IsValid() const { return Hierarchy != nullptr && Key.Value != 0; }
	TransformHierarchyService* GetService() const { return Hierarchy; }
	DataBatchKey GetKey() const { return Key; }

private:
	TransformHierarchyService* Hierarchy = nullptr;
	DataBatchKey Key{};
};
