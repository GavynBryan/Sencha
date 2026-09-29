#pragma once

#include <functional>
#include <string>
#include <vector>

class DataDocumentSet;
class World;
struct RuntimeAssets;

// The gameplay tag names the project's content declares, unsaved declarations included.
class AnimationContentTags
{
public:
    AnimationContentTags() = default;
    AnimationContentTags(const AnimationContentTags&) = delete;
    AnimationContentTags& operator=(const AnimationContentTags&) = delete;
    AnimationContentTags(AnimationContentTags&&) = delete;
    AnimationContentTags& operator=(AnimationContentTags&&) = delete;

    void Refresh(RuntimeAssets& assets, const DataDocumentSet& documents);
    [[nodiscard]] const std::vector<std::string>& Names() const { return Declared; }
    [[nodiscard]] const std::vector<std::string>& Errors() const { return Problems; }
    // `module`, then these names; the names are read as each World is built, so a refresh reaches later Worlds.
    [[nodiscard]] std::function<void(World&)> Vocabulary(std::function<void(World&)> module) const;

private:
    std::vector<std::string> Declared;
    std::vector<std::string> Problems;
};
