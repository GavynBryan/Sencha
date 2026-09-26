#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class DataDocument;
struct RuntimeAssets;

enum class DataResidentStatus
{
    Current,
    Pending,
    KeptLastValid,
};

struct DataResidentState
{
    DataResidentStatus Status = DataResidentStatus::Pending;
    std::string Error;
};

// Keeps each resident data asset on its document's committed working version,
// on the owner thread, without ever waiting for an asset to load.
class DataResidentSync
{
public:
    explicit DataResidentSync(RuntimeAssets& assets) : Assets(assets) {}

    DataResidentSync(const DataResidentSync&) = delete;
    DataResidentSync& operator=(const DataResidentSync&) = delete;
    DataResidentSync(DataResidentSync&&) = delete;
    DataResidentSync& operator=(DataResidentSync&&) = delete;

    // True when any resident asset changed, this document's or one that was waiting.
    [[nodiscard]] bool Push(const DataDocument& document);
    [[nodiscard]] bool PushWaiting();

    // Before a document closes, reloads or takes the file's version.
    void Forget(const DataDocument& document);
    void RestoreFromFile(const DataDocument& document);

    [[nodiscard]] const DataResidentState* StateOf(const DataDocument& document) const;

private:
    struct WaitingPush
    {
        const DataDocument* Document = nullptr;
        std::uint64_t Generation = 0;
    };

    [[nodiscard]] bool IsResident(const DataDocument& document) const;
    [[nodiscard]] bool Apply(const DataDocument& document);

    RuntimeAssets& Assets;
    std::vector<WaitingPush> Waiting;
    std::unordered_map<const DataDocument*, DataResidentState> States;
};
