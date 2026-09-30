#include "actions/ActionCatalog.h"

#include <algorithm>

bool ActionCatalog::Define(ActionDefinition definition, std::string* error)
{
    if (const Entry* existing = FindEntry(definition.Id))
    {
        if (existing->Definition == definition)
            return true;
        if (error != nullptr)
            *error = "action '" + definition.Id + "' is already defined differently";
        return false;
    }

    auto entry = std::make_unique<Entry>();
    entry->Definition = std::move(definition);
    const auto keymapped = Keymap.find(entry->Definition.Id);
    entry->Chords = keymapped != Keymap.end() ? std::vector<KeyChord>{ keymapped->second }
                                              : entry->Definition.DefaultChords;
    Entries.push_back(std::move(entry));
    if (DefinedObserver)
        DefinedObserver(Entries.back()->Definition);
    return true;
}

const ActionDefinition* ActionCatalog::Find(std::string_view id) const
{
    const Entry* entry = FindEntry(id);
    return entry != nullptr ? &entry->Definition : nullptr;
}

std::vector<const ActionDefinition*> ActionCatalog::All() const
{
    std::vector<const ActionDefinition*> all;
    all.reserve(Entries.size());
    for (const std::unique_ptr<Entry>& entry : Entries)
        all.push_back(&entry->Definition);
    return all;
}

std::vector<std::string> ActionCatalog::ApplyKeymap(const std::unordered_map<std::string, KeyChord>& overrides)
{
    // Kept, so an action defined after the keymap was read -- a workspace
    // opened later -- still takes the user's chord.
    Keymap = overrides;
    std::vector<std::string> unknown;
    for (const auto& [id, chord] : overrides)
    {
        auto it = std::find_if(Entries.begin(), Entries.end(),
                               [&](const std::unique_ptr<Entry>& e) { return e->Definition.Id == id; });
        if (it == Entries.end())
            unknown.push_back(id);
        else
            (*it)->Chords = { chord };
    }
    std::sort(unknown.begin(), unknown.end());
    return unknown;
}

std::span<const KeyChord> ActionCatalog::Chords(std::string_view id) const
{
    const Entry* entry = FindEntry(id);
    if (entry == nullptr)
        return {};
    return entry->Chords;
}

std::string ActionCatalog::ChordText(std::string_view id) const
{
    const std::span<const KeyChord> chords = Chords(id);
    return chords.empty() ? std::string{} : FormatKeyChord(chords.front());
}

const ActionDefinition* ActionCatalog::ForChord(const KeyChord& chord) const
{
    for (const std::unique_ptr<Entry>& entry : Entries)
    {
        if (std::find(entry->Chords.begin(), entry->Chords.end(), chord) != entry->Chords.end())
            return &entry->Definition;
    }
    return nullptr;
}

std::vector<ActionChordConflict> ActionCatalog::Conflicts() const
{
    std::vector<ActionChordConflict> conflicts;
    for (std::size_t a = 0; a < Entries.size(); ++a)
    {
        for (const KeyChord& chord : Entries[a]->Chords)
        {
            for (std::size_t b = a + 1; b < Entries.size(); ++b)
            {
                const std::vector<KeyChord>& other = Entries[b]->Chords;
                if (std::find(other.begin(), other.end(), chord) != other.end())
                    conflicts.push_back({ chord, Entries[a]->Definition.Id, Entries[b]->Definition.Id });
            }
        }
    }
    return conflicts;
}

void ActionCatalog::SetDefinedObserver(std::function<void(const ActionDefinition&)> observer)
{
    DefinedObserver = std::move(observer);
}

const ActionCatalog::Entry* ActionCatalog::FindEntry(std::string_view id) const
{
    for (const std::unique_ptr<Entry>& entry : Entries)
    {
        if (entry->Definition.Id == id)
            return entry.get();
    }
    return nullptr;
}
