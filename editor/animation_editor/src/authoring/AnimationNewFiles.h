#pragma once

#include "authoring/AnimationRigRecipe.h"

#include <filesystem>
#include <string>
#include <vector>

class AssetRegistry;

// Refuses before writing anything if any of the documents already exists;
// each written data document is registered.
bool WriteAnimationNewDocuments(const std::filesystem::path& root, AssetRegistry& registry,
                                const std::vector<AnimationNewDocument>& documents, std::string& error);
bool WriteAnimationNewFile(const std::filesystem::path& root, const AnimationNewDocument& document, int indent,
                           std::string& error);
