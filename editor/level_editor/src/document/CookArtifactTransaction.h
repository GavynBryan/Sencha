#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Stages a set of cooked files under the assets root and replaces their active
// paths as one rollback-capable commit. Callers register files in dependency
// order and register the cooked scene or world manifest last.
class CookArtifactTransaction
{
public:
    CookArtifactTransaction(const std::filesystem::path& assetsRoot,
                            std::string_view target);
    ~CookArtifactTransaction();

    CookArtifactTransaction(const CookArtifactTransaction&) = delete;
    CookArtifactTransaction& operator=(const CookArtifactTransaction&) = delete;

    [[nodiscard]] const std::filesystem::path& StagingRoot() const { return Staging; }

    // Returns the staging path corresponding to an active path and registers it
    // for replacement. Repeated calls for the same active path return one path.
    [[nodiscard]] std::filesystem::path Stage(const std::filesystem::path& activePath);

    // Copies an existing active file into staging before a writer updates it.
    // A missing source succeeds and leaves the staged path absent.
    [[nodiscard]] bool Seed(const std::filesystem::path& activePath,
                            std::string* error = nullptr);

    // Removes an active file on successful commit without discarding the
    // rollback copy until the entire transaction has completed.
    void Withdraw(const std::filesystem::path& activePath);

    [[nodiscard]] bool Commit(std::string* error = nullptr);
    void Discard();

private:
    struct Entry
    {
        std::filesystem::path Active;
        std::filesystem::path Staged;
        std::filesystem::path Backup;
        bool Withdrawn = false;
        bool HadActive = false;
        bool Installed = false;
    };

    [[nodiscard]] Entry* Find(const std::filesystem::path& activePath);
    void RollBack();

    std::filesystem::path Root;
    std::filesystem::path Staging;
    std::vector<Entry> Entries;
    bool Finished = false;
};

