#pragma once

#include "CookArtifactTransaction.h"

#include <assets/cook/AssetImporter.h>
#include <assets/cook/CookedCache.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

// Publishes a prepared asset import through a document cook's transaction: byte
// writes stage as transaction entries and index entries land in the document's
// single staged index. Referenced-asset imports then commit atomically with the
// rest of the document publication instead of mutating active state early.
class DocumentImportPublisher final : public IImportPublisher
{
public:
    DocumentImportPublisher(CookArtifactTransaction& transaction,
                            std::filesystem::path assetsRoot,
                            CookedCacheIndex& index)
        : Transaction(transaction), Root(std::move(assetsRoot)), Index(index)
    {
    }

    bool WriteBytes(std::string_view fileRelPath,
                    std::span<const std::byte> bytes) override
    {
        const std::filesystem::path staged = Transaction.Stage(Root / fileRelPath);
        std::error_code ec;
        std::filesystem::create_directories(staged.parent_path(), ec);
        std::ofstream file(staged, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return false;
        if (!bytes.empty())
            file.write(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
        return file.good();
    }

    void PutIndexEntry(CookedSourceEntry entry) override
    {
        Index.Put(std::move(entry));
    }

private:
    CookArtifactTransaction& Transaction;
    std::filesystem::path Root;
    CookedCacheIndex& Index;
};
