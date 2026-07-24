#include "CookArtifactTransaction.h"

#include <core/hash/ContentHash.h>

#include <chrono>
#include <format>

namespace
{
    bool Fail(std::string* error, std::string message)
    {
        if (error != nullptr)
            *error = std::move(message);
        return false;
    }

    std::string SafeTarget(std::string_view target)
    {
        return std::format("{:016x}", HashBytes64(target));
    }
}

CookArtifactTransaction::CookArtifactTransaction(
    const std::filesystem::path& assetsRoot, std::string_view target)
    : Root(std::filesystem::absolute(assetsRoot).lexically_normal())
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    Staging = Root / ".cooked/.staging"
        / (SafeTarget(target) + "_" + std::to_string(nonce));
    std::error_code ec;
    std::filesystem::create_directories(Staging, ec);
}

CookArtifactTransaction::~CookArtifactTransaction()
{
    if (!Finished)
        Discard();
}

CookArtifactTransaction::Entry* CookArtifactTransaction::Find(
    const std::filesystem::path& activePath)
{
    const std::filesystem::path normalized =
        std::filesystem::absolute(activePath).lexically_normal();
    for (Entry& entry : Entries)
        if (entry.Active == normalized)
            return &entry;
    return nullptr;
}

std::filesystem::path CookArtifactTransaction::Stage(
    const std::filesystem::path& activePath)
{
    if (Entry* existing = Find(activePath))
        return existing->Staged;

    const std::filesystem::path active =
        std::filesystem::absolute(activePath).lexically_normal();
    std::error_code ec;
    std::filesystem::path relative = std::filesystem::relative(active, Root, ec);
    if (ec || relative.empty() || *relative.begin() == "..")
        relative = std::filesystem::path("external") / SafeTarget(active.generic_string());
    const std::filesystem::path staged = Staging / "files" / relative;
    const std::filesystem::path backup = Staging / "backups" / relative;
    std::filesystem::create_directories(staged.parent_path(), ec);
    Entries.push_back(Entry{ active, staged, backup });
    return staged;
}

bool CookArtifactTransaction::Seed(const std::filesystem::path& activePath,
                                   std::string* error)
{
    const std::filesystem::path staged = Stage(activePath);
    std::error_code ec;
    if (!std::filesystem::exists(activePath, ec))
        return true;
    std::filesystem::create_directories(staged.parent_path(), ec);
    std::filesystem::copy_file(activePath, staged,
                               std::filesystem::copy_options::overwrite_existing, ec);
    return !ec || Fail(error, "could not seed staged file '"
        + staged.generic_string() + "'");
}

void CookArtifactTransaction::Withdraw(const std::filesystem::path& activePath)
{
    (void)Stage(activePath);
    Find(activePath)->Withdrawn = true;
}

bool CookArtifactTransaction::Commit(std::string* error)
{
    if (Finished)
        return Fail(error, "cook artifact transaction is already finished");

    std::error_code ec;
    for (Entry& entry : Entries)
    {
        if (!entry.Withdrawn && !std::filesystem::exists(entry.Staged, ec))
        {
            RollBack();
            Finished = true;
            return Fail(error, "staged cook artifact is missing '"
                + entry.Staged.generic_string() + "'");
        }

        entry.HadActive = std::filesystem::exists(entry.Active, ec);
        if (entry.HadActive)
        {
            std::filesystem::create_directories(entry.Backup.parent_path(), ec);
            std::filesystem::rename(entry.Active, entry.Backup, ec);
            if (ec)
            {
                RollBack();
                Finished = true;
                return Fail(error, "could not back up active cook artifact '"
                    + entry.Active.generic_string() + "'");
            }
        }

        if (!entry.Withdrawn)
        {
            std::filesystem::create_directories(entry.Active.parent_path(), ec);
            std::filesystem::rename(entry.Staged, entry.Active, ec);
            if (ec)
            {
                RollBack();
                Finished = true;
                return Fail(error, "could not publish cook artifact '"
                    + entry.Active.generic_string() + "'");
            }
            entry.Installed = true;
        }
    }

    Finished = true;
    std::filesystem::remove_all(Staging, ec);
    return true;
}

void CookArtifactTransaction::RollBack()
{
    std::error_code ec;
    for (auto it = Entries.rbegin(); it != Entries.rend(); ++it)
    {
        Entry& entry = *it;
        if (entry.Installed)
            std::filesystem::remove(entry.Active, ec);
        if (entry.HadActive && std::filesystem::exists(entry.Backup, ec))
        {
            std::filesystem::create_directories(entry.Active.parent_path(), ec);
            std::filesystem::rename(entry.Backup, entry.Active, ec);
        }
    }
}

void CookArtifactTransaction::Discard()
{
    if (Finished)
        return;
    RollBack();
    std::error_code ec;
    std::filesystem::remove_all(Staging, ec);
    Finished = true;
}

