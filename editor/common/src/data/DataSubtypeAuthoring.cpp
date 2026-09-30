#include "data/DataSubtypeAuthoring.h"

bool DataSubtypeAuthoringRegistry::Register(std::unique_ptr<IDataSubtypeAuthoring> authoring)
{
    if (authoring == nullptr || Find(authoring->Subtype()) != nullptr)
        return false;
    Entries.push_back(std::move(authoring));
    return true;
}

IDataSubtypeAuthoring* DataSubtypeAuthoringRegistry::Find(std::string_view subtype) const
{
    for (const std::unique_ptr<IDataSubtypeAuthoring>& entry : Entries)
        if (entry->Subtype() == subtype)
            return entry.get();
    return nullptr;
}
