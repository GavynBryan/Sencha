#pragma once

#include "properties/PropertySource.h"
#include "workspaces/WorkspaceFacets.h"

#include <core/json/JsonValue.h>

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

union SDL_Event;
struct DataSchema;
class DataDocument;
class DataDocumentSet;

//=============================================================================
// IDataSubtypeAuthoring
//
// What one data subtype adds to its schema's properties so its documents read
// as what they are: a keymap as bindings on controls, an action set as the
// vocabulary it declares. It reshapes the laid-out properties, takes the
// writes and verbs whose meaning it knows better than the schema does, and
// may put previews beside the document. It never draws: whatever presents the
// properties presents what it adds.
//=============================================================================
class IDataSubtypeAuthoring
{
public:
    virtual ~IDataSubtypeAuthoring() = default;

    [[nodiscard]] virtual std::string_view Subtype() const = 0;

    // Before its properties are read: refreshes what they are read against,
    // such as the action set a profile names.
    virtual void Update(const DataDocument& /*document*/, DataDocumentSet& /*documents*/) {}
    // The schema's properties, reshaped: retitled, hidden, given choices,
    // candidates or verbs, joined by keys of its own.
    virtual void Refine(std::vector<PropertyNode>& /*nodes*/, const DataDocument& /*document*/,
                        const DataSchema& /*schema*/) const
    {
    }
    // A write it takes itself, made to the document's data object; nothing
    // when the key is not its to take, false when the value is refused.
    [[nodiscard]] virtual std::optional<bool> Write(std::string_view /*key*/, std::string_view /*value*/,
                                                    JsonValue& /*data*/, const DataSchema& /*schema*/) const
    {
        return std::nullopt;
    }
    [[nodiscard]] virtual std::optional<std::vector<std::string>> Candidates(std::string_view /*key*/,
                                                                             const JsonValue& /*data*/) const
    {
        return std::nullopt;
    }
    // A verb it takes that changes the data, made to the document's data object
    // as one step; nothing when the verb is not its to take, false when refused.
    [[nodiscard]] virtual std::optional<bool> Perform(std::string_view /*key*/, std::string_view /*verb*/,
                                                      std::string_view /*choice*/, JsonValue& /*data*/) const
    {
        return std::nullopt;
    }
    // A verb it takes that changes no data, such as listening for a control.
    [[nodiscard]] virtual std::optional<PropertyEditResult> Act(std::string_view /*key*/, std::string_view /*verb*/,
                                                                const DataDocument& /*document*/)
    {
        return std::nullopt;
    }
    // Offered before the window's UI sees it; true claims it.
    virtual bool HandlePlatformEvent(const SDL_Event& /*event*/) { return false; }
    // A write it wants made on its own, such as a control just pressed, as the
    // key and value to write; handed over once.
    [[nodiscard]] virtual std::optional<std::pair<std::string, std::string>> TakeWrite(const DataDocument& /*document*/)
    {
        return std::nullopt;
    }
    // Property sections beside the document's own: previews read from it.
    [[nodiscard]] virtual std::vector<PropertySection> Sections(const DataDocument& /*document*/) { return {}; }
};

// The subtypes that add to their schema's properties, one each.
class DataSubtypeAuthoringRegistry
{
public:
    // Refuses a second for one subtype, which would race the first.
    bool Register(std::unique_ptr<IDataSubtypeAuthoring> authoring);
    [[nodiscard]] IDataSubtypeAuthoring* Find(std::string_view subtype) const;

private:
    std::vector<std::unique_ptr<IDataSubtypeAuthoring>> Entries;
};

// Fills a registry with every subtype's authoring. The application composes
// it from each workspace library's own, and each workspace that shows data
// documents keeps a registry of its own filled by it.
using DataSubtypeAuthoringSetup = std::function<void(DataSubtypeAuthoringRegistry&)>;
