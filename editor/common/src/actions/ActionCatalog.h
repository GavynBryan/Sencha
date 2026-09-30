#pragma once

#include "actions/ActionTone.h"
#include "icons/IconId.h"
#include "input/KeymapFile.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// What an operation is, independent of who can perform it right now: the id a
// keymap, a console command, a menu and a palette all name it by, how it is
// labelled, and the chords it answers to.
struct ActionDefinition
{
    std::string Id;
    std::string Label;
    std::string Category;
    IconId Icon = IconId::None;
    ActionTone Tone = ActionTone::Normal;
    std::vector<KeyChord> DefaultChords = {};

    bool operator==(const ActionDefinition&) const = default;
};

struct ActionChordConflict
{
    KeyChord Chord;
    std::string First;
    std::string Second;
};

//=============================================================================
// ActionCatalog
//
// Every operation the editor offers, as data. No callbacks: whether an action
// can run, and what it does, is answered by whichever ActionContext currently
// claims it, so nothing here outlives the objects that perform the work.
//
// A chord belongs to one action. `edit.delete` is one action whether it deletes
// a brush, an asset or an event; the context that claims it decides which.
// Two actions answering one chord is a conflict, reported rather than resolved
// by registration order.
//=============================================================================
class ActionCatalog
{
public:
    // Defining an id twice is refused unless the definitions are equal, which
    // is what a workspace reopening does.
    bool Define(ActionDefinition definition, std::string* error = nullptr);

    // An action named for data that is renamed, as a cook profile is. False
    // when nothing has that id.
    bool Relabel(std::string_view id, std::string label);

    [[nodiscard]] const ActionDefinition* Find(std::string_view id) const;
    [[nodiscard]] std::vector<const ActionDefinition*> All() const;

    // The user's keymap: an entry replaces its action's chords. Ids that name
    // no defined action are returned, so a typo in keybinds.json is reported.
    std::vector<std::string> ApplyKeymap(const std::unordered_map<std::string, KeyChord>& overrides);

    [[nodiscard]] std::span<const KeyChord> Chords(std::string_view id) const;
    // The first chord's text, empty for an action with none.
    [[nodiscard]] std::string ChordText(std::string_view id) const;
    [[nodiscard]] const ActionDefinition* ForChord(const KeyChord& chord) const;
    [[nodiscard]] std::vector<ActionChordConflict> Conflicts() const;

    // Told of every action as it is first defined, so a host can give each a
    // console command of the same id.
    void SetDefinedObserver(std::function<void(const ActionDefinition&)> observer);

private:
    struct Entry
    {
        ActionDefinition Definition;
        std::vector<KeyChord> Chords;
    };

    [[nodiscard]] const Entry* FindEntry(std::string_view id) const;

    std::vector<std::unique_ptr<Entry>> Entries;
    std::unordered_map<std::string, KeyChord> Keymap;
    std::function<void(const ActionDefinition&)> DefinedObserver;
};
