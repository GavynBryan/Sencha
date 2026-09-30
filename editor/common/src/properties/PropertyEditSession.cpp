#include "properties/PropertyEditSession.h"

#include <utility>

bool PropertyEditSession::Begin(IPropertySource& source, std::string key)
{
    Cancel();
    if (!source.BeginEdit(key))
        return false;
    Source = &source;
    EditingKey = std::move(key);
    return true;
}

PropertyEditResult PropertyEditSession::Preview(std::string_view value)
{
    if (Source == nullptr)
        return PropertyEditResult::Gone;
    return Source->PreviewEdit(EditingKey, value);
}

PropertyEditResult PropertyEditSession::Commit(std::string_view value)
{
    if (Source == nullptr)
        return PropertyEditResult::Gone;
    IPropertySource* source = std::exchange(Source, nullptr);
    const PropertyEditResult result = source->CommitEdit(EditingKey, value);
    if (result != PropertyEditResult::Applied)
        source->CancelEdit(EditingKey);
    EditingKey.clear();
    return result;
}

void PropertyEditSession::Cancel()
{
    if (Source == nullptr)
        return;
    std::exchange(Source, nullptr)->CancelEdit(EditingKey);
    EditingKey.clear();
}

PropertyEditResult PropertyEditSession::Set(IPropertySource& source, std::string key, std::string_view value)
{
    if (!Begin(source, std::move(key)))
        return PropertyEditResult::Gone;
    return Commit(value);
}
