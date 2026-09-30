#pragma once

#include <algorithm>
#include <concepts>
#include <span>
#include <vector>

// Which keys the UI showed in its last pass. The UI marks what it draws; the
// render side advances once per frame, before rendering, and then renders only
// what was shown -- so anything a panel stops showing costs nothing from the
// next frame on, whatever the reason it stopped.
template <std::equality_comparable Key>
class DisplayLedger
{
public:
    void MarkDisplayed(Key key)
    {
        if (std::ranges::find(Marked, key) == Marked.end())
            Marked.push_back(key);
    }

    void Advance()
    {
        Shown.swap(Marked);
        Marked.clear();
    }

    [[nodiscard]] bool WasDisplayed(Key key) const { return std::ranges::find(Shown, key) != Shown.end(); }
    [[nodiscard]] std::span<const Key> Displayed() const { return Shown; }

    void Forget(Key key)
    {
        std::erase(Marked, key);
        std::erase(Shown, key);
    }

private:
    std::vector<Key> Marked;
    std::vector<Key> Shown;
};
