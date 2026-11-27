#include <save/SaveSlotListing.h>

#include <utility>

namespace
{
    struct SlotPrefix
    {
        SaveSlotSummary Summary;
        std::optional<SavedGameChunk> Thumbnail;
        std::uint64_t BytesRead = 0;
    };

    bool Take(std::optional<std::vector<std::byte>> prefix, SlotPrefix& read, DecodedSavedGameSummary& decoded)
    {
        if (!prefix.has_value())
            return false;
        read.BytesRead += prefix->size();
        decoded = DecodeSavedGameSummary(*prefix);
        if (!decoded.Summary.has_value())
            return false;
        read.Summary.Summary = std::move(*decoded.Summary);
        read.Summary.HasThumbnail = decoded.Thumbnail.has_value();
        read.Thumbnail = std::move(decoded.Thumbnail);
        return true;
    }

    SlotPrefix ReadSlotPrefix(const SaveSlotFiles& files, std::string_view slot)
    {
        SlotPrefix read;
        read.Summary.Slot = std::string(slot);
        DecodedSavedGameSummary current;
        std::optional<std::vector<std::byte>> file = files.ReadPrefix(slot, SavedGamePrefixWanted);
        const bool fileExists = file.has_value();
        if (Take(std::move(file), read, current))
            return read;
        read.Summary.Error = fileExists ? std::move(current.Error) : "slot '" + read.Summary.Slot + "' has no file";
        read.Summary.Newer = current.Newer;
        if (current.Newer)
            return read;
        DecodedSavedGameSummary backup;
        std::optional<std::vector<std::byte>> backupFile = files.ReadBackupPrefix(slot, SavedGamePrefixWanted);
        const bool backupExists = backupFile.has_value();
        if (Take(std::move(backupFile), read, backup))
            read.Summary.FromBackup = true;
        else if (backup.Newer)
        {
            read.Summary.Newer = true;
            read.Summary.Error = std::move(backup.Error);
        }
        else if (!fileExists && !backupExists)
            read.Summary.Error = "no save in slot '" + read.Summary.Slot + "'";
        return read;
    }
}

SaveSlotListing ListSaveSlots(const SaveSlotFiles& files)
{
    SaveSlotListing listing;
    for (const std::string& slot : files.Slots())
    {
        SlotPrefix read = ReadSlotPrefix(files, slot);
        listing.BytesRead += read.BytesRead;
        listing.Slots.push_back(std::move(read.Summary));
    }
    return listing;
}

SaveSlotSummary ReadSaveSlotSummary(const SaveSlotFiles& files, std::string_view slot)
{
    return ReadSlotPrefix(files, slot).Summary;
}

std::optional<SavedGameThumbnail> ReadSaveSlotThumbnail(const SaveSlotFiles& files, std::string_view slot)
{
    const SlotPrefix read = ReadSlotPrefix(files, slot);
    return read.Thumbnail.has_value() ? DecodeSavedGameThumbnail(*read.Thumbnail) : std::nullopt;
}
