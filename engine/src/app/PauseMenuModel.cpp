#include <app/PauseMenuModel.h>

#include <algorithm>
#include <utility>

namespace
{
    constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);
}

std::size_t PauseMenuModel::IndexOf(PauseCommandId command) const
{
    for (std::size_t i = 0; i < Entries_.size(); ++i)
    {
        if (Entries_[i].Command == command)
            return i;
    }
    return kNoIndex;
}

PauseCommandId PauseMenuModel::Add(std::string label, PauseCommandHandler handler)
{
    const PauseCommandId command{ NextCommand++ };
    PauseMenuEntry entry;
    entry.Label = std::move(label);
    entry.Command = command;
    Entries_.push_back(std::move(entry));
    Handlers_.emplace_back(command, std::move(handler));
    return command;
}

bool PauseMenuModel::SetHandler(PauseCommandId command, PauseCommandHandler handler)
{
    // One behaviour per entry. A game replacing Quit with its own confirmation
    // is saying the authored binding no longer applies, and running both would
    // ask the host to exit twice.
    if (const std::size_t index = IndexOf(command); index != kNoIndex)
        Entries_[index].Binding = {};

    for (auto& [id, existing] : Handlers_)
    {
        if (id == command)
        {
            existing = std::move(handler);
            return true;
        }
    }
    // A handler for a command with no entry is not an error: a game may install
    // one before adding the entry that reaches it.
    Handlers_.emplace_back(command, std::move(handler));
    return true;
}

bool PauseMenuModel::SetBinding(PauseCommandId command, VerbBindingKey binding)
{
    const std::size_t index = IndexOf(command);
    if (index == kNoIndex)
        return false;

    Entries_[index].Binding = binding;
    // The other half of the same rule: assigning authored behaviour drops the
    // native one, rather than leaving a handler that will never be reached.
    if (binding.IsValid())
    {
        for (auto& [id, handler] : Handlers_)
        {
            if (id == command)
                handler = {};
        }
    }
    return true;
}

VerbBindingKey PauseMenuModel::Binding(PauseCommandId command) const
{
    const std::size_t index = IndexOf(command);
    return index == kNoIndex ? VerbBindingKey{} : Entries_[index].Binding;
}

bool PauseMenuModel::SetLabel(PauseCommandId command, std::string label)
{
    const std::size_t index = IndexOf(command);
    if (index == kNoIndex)
        return false;
    Entries_[index].Label = std::move(label);
    return true;
}

bool PauseMenuModel::SetEnabled(PauseCommandId command, bool enabled)
{
    const std::size_t index = IndexOf(command);
    if (index == kNoIndex)
        return false;
    Entries_[index].Enabled = enabled;
    return true;
}

bool PauseMenuModel::Remove(PauseCommandId command)
{
    const std::size_t index = IndexOf(command);
    if (index == kNoIndex)
        return false;
    Entries_.erase(Entries_.begin() + static_cast<std::ptrdiff_t>(index));
    // The handler stays. Removing an entry hides a command rather than
    // forgetting what it does, so a game that puts it back does not have to
    // supply the behaviour again.
    return true;
}

bool PauseMenuModel::MoveBefore(PauseCommandId command, PauseCommandId before)
{
    const std::size_t from = IndexOf(command);
    if (from == kNoIndex)
        return false;
    const std::size_t to = IndexOf(before);
    if (to == kNoIndex || from == to)
        return false;

    PauseMenuEntry moved = std::move(Entries_[from]);
    Entries_.erase(Entries_.begin() + static_cast<std::ptrdiff_t>(from));
    // Re-found, because erasing above may have shifted it.
    const std::size_t target = IndexOf(before);
    Entries_.insert(Entries_.begin() + static_cast<std::ptrdiff_t>(target), std::move(moved));
    return true;
}

std::vector<std::string> PauseMenuModel::Labels() const
{
    std::vector<std::string> labels;
    labels.reserve(Entries_.size());
    for (const PauseMenuEntry& entry : Entries_)
        labels.push_back(entry.Label);
    return labels;
}

PauseCommandId PauseMenuModel::CommandAt(std::size_t index) const
{
    return index < Entries_.size() ? Entries_[index].Command : PauseCommandId{};
}

const PauseCommandHandler* PauseMenuModel::Handler(PauseCommandId command) const
{
    for (const auto& [id, handler] : Handlers_)
    {
        if (id == command && handler)
            return &handler;
    }
    return nullptr;
}

bool PauseMenuModel::IsEnabled(PauseCommandId command) const
{
    const std::size_t index = IndexOf(command);
    return index != kNoIndex && Entries_[index].Enabled;
}

void PauseMenuModel::SetOptionsPage(std::string package)
{
    OptionsPage_ = std::move(package);
    // The entry exists exactly when there is a page behind it, which is what
    // keeps Options from being a button that does nothing.
    if (OptionsPage_.empty())
    {
        (void)Remove(kPauseOptions);
        return;
    }
    if (IndexOf(kPauseOptions) != kNoIndex)
        return;

    // Between Resume and the way out, which is where a player looks for it.
    PauseMenuEntry options;
    options.Label = "Options";
    options.Command = kPauseOptions;
    Entries_.push_back(std::move(options));
    if (IndexOf(kPauseExit) != kNoIndex)
        (void)MoveBefore(kPauseOptions, kPauseExit);
}

void PauseMenuModel::InstallDefaults(std::string_view exitLabel)
{
    Entries_.clear();
    PauseMenuEntry resume;
    resume.Label = "Resume";
    resume.Command = kPauseResume;
    Entries_.push_back(std::move(resume));

    // Options is added by SetOptionsPage, when a page exists to reach.

    // How you leave is the host's to name: a desktop says "Exit to Desktop",
    // and a host that cannot terminate at all passes nothing and gets no entry.
    // The command stays platform-neutral either way -- it asks the application
    // to end, and what that means is the host's answer, not the menu's.
    if (!exitLabel.empty())
    {
        PauseMenuEntry exit;
        exit.Label = std::string(exitLabel);
        exit.Command = kPauseExit;
        Entries_.push_back(std::move(exit));
    }
}
