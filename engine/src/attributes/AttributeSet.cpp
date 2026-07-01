#include <attributes/AttributeSet.h>

#include <cassert>

namespace
{
    int LowerBound(const AttributeId* ids, int count, AttributeId id)
    {
        int lo = 0;
        int hi = count;
        while (lo < hi)
        {
            const int mid = lo + (hi - lo) / 2;
            if (ids[mid].Value < id.Value)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }
}

bool AttributeSet::Has(AttributeId id) const
{
    const int i = LowerBound(Ids, Count, id);
    return i < Count && Ids[i] == id;
}

float AttributeSet::GetBase(AttributeId id, float fallback) const
{
    const int i = LowerBound(Ids, Count, id);
    return (i < Count && Ids[i] == id) ? Base[i] : fallback;
}

float AttributeSet::GetCurrent(AttributeId id, float fallback) const
{
    const int i = LowerBound(Ids, Count, id);
    return (i < Count && Ids[i] == id) ? Current[i] : fallback;
}

float* AttributeSet::BasePtr(AttributeId id)
{
    const int i = LowerBound(Ids, Count, id);
    return (i < Count && Ids[i] == id) ? &Base[i] : nullptr;
}

float* AttributeSet::CurrentPtr(AttributeId id)
{
    const int i = LowerBound(Ids, Count, id);
    return (i < Count && Ids[i] == id) ? &Current[i] : nullptr;
}

bool AttributeSet::SetBase(AttributeId id, float base)
{
    const int i = LowerBound(Ids, Count, id);
    if (i < Count && Ids[i] == id)
    {
        Base[i] = base;
        return true;
    }
    return false;
}

bool AttributeSet::Add(AttributeId id, float base)
{
    if (!id.IsValid())
        return false;

    const int i = LowerBound(Ids, Count, id);
    if (i < Count && Ids[i] == id)
        return false; // already present

    if (Count >= Capacity)
    {
        assert(false && "AttributeSet capacity exceeded");
        return false;
    }

    for (int j = Count; j > i; --j)
    {
        Ids[j] = Ids[j - 1];
        Base[j] = Base[j - 1];
        Current[j] = Current[j - 1];
    }
    Ids[i] = id;
    Base[i] = base;
    Current[i] = base;
    ++Count;
    return true;
}
