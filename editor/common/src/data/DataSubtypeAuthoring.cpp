#include "data/DataSubtypeAuthoring.h"

PropertyEditResult PropertyEditResultOf(DataSubtypeEdit edit)
{
    switch (edit)
    {
    case DataSubtypeEdit::Applied:   return PropertyEditResult::Applied;
    case DataSubtypeEdit::Unchanged: return PropertyEditResult::Unchanged;
    case DataSubtypeEdit::Refused:   return PropertyEditResult::Refused;
    case DataSubtypeEdit::NotMine:
    case DataSubtypeEdit::Gone:      return PropertyEditResult::Gone;
    }
    return PropertyEditResult::Gone;
}

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
