#pragma once

#include "properties/PropertySource.h"
#include "workspaces/WorkspaceFacets.h"

#include <core/json/JsonValue.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

union SDL_Event;
struct DataSchema;
class DataDocument;
class DataDocumentSet;

// What a subtype made of a write or a verb: not its to take, or taken with
// what came of it.
enum class DataSubtypeEdit : std::uint8_t
{
    NotMine,
    Applied,
    Unchanged,
    Refused,
    Gone,
};

[[nodiscard]] constexpr DataSubtypeEdit DataSubtypeEditOf(bool applied)
{
    return applied ? DataSubtypeEdit::Applied : DataSubtypeEdit::Refused;
}

// A taken edit's result as the property it was made through reports it.
[[nodiscard]] PropertyEditResult PropertyEditResultOf(DataSubtypeEdit edit);

// What one data subtype adds to its schema's properties so its documents read
// as what they are (editor/ARCHITECTURE.md, Data workspace). It never draws.
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
    // A write it takes itself, made to the document's data object.
    [[nodiscard]] virtual DataSubtypeEdit Write(std::string_view /*key*/, std::string_view /*value*/,
                                                JsonValue& /*data*/, const DataSchema& /*schema*/) const
    {
        return DataSubtypeEdit::NotMine;
    }
    [[nodiscard]] virtual std::optional<std::vector<std::string>> Candidates(std::string_view /*key*/,
                                                                             const JsonValue& /*data*/) const
    {
        return std::nullopt;
    }
    // A verb it takes that changes the data, made to the data object as one step.
    [[nodiscard]] virtual DataSubtypeEdit Perform(std::string_view /*key*/, std::string_view /*verb*/,
                                                  std::string_view /*choice*/, JsonValue& /*data*/) const
    {
        return DataSubtypeEdit::NotMine;
    }
    // A verb it takes that changes no data, such as listening for a control.
    [[nodiscard]] virtual DataSubtypeEdit Act(std::string_view /*key*/, std::string_view /*verb*/,
                                              const DataDocument& /*document*/)
    {
        return DataSubtypeEdit::NotMine;
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
