#pragma once

#include <cstdint>
#include <string>
#include <vector>

class IPropertySource;

// Properties the shell shows together, under a heading.
struct PropertySection
{
    std::string Heading = {};
    // Owned by the workspace, valid until its next frame.
    IPropertySource* Source = nullptr;
};

// One property of a source, named where something other than a sheet edits it.
struct PropertyRef
{
    IPropertySource* Source = nullptr;
    std::string Key = {};
};

// What is selected, as the shell presents it: what to call it, the actions
// that apply to it now, and its properties in sections. While an edit is
// staged (a previewed inset, bevel or bridge) the actions are how it applies
// or cancels.
struct WorkspaceSelectionFacet
{
    // Empty when nothing is selected.
    std::string Title = {};
    // Moves on whenever what is selected does, so anything held for the last
    // selection ends; 0 where selections are told apart by their title alone.
    std::uint64_t Revision = 0;
    // A line under it: how the selection is known, where it came from.
    std::string Subtitle = {};
    // The property the title is, when renaming it is an edit; empty when the
    // title only describes (three objects, five faces).
    PropertyRef TitleProperty = {};
    std::vector<std::string> Actions = {};
    std::vector<PropertySection> Sections = {};
    // Everything else about it, for when it is looked at closely: an object's
    // components, each a top-level group.
    std::vector<PropertySection> Details = {};
    // The property to bring into view and mark, by its key: a problem's field.
    std::string Focus = {};
    bool Staged = false;
    // The properties are the work itself rather than a note about something in
    // the content: a data asset is its fields.
    bool Primary = false;
    // Looked at only when asked for -- the context wheel's More..., or the
    // palette -- rather than shown whenever something is selected; an edit
    // being staged still shows it, since that is how it applies.
    bool OnRequest = false;
};
