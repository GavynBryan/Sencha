#include "render/DisplayLedger.h"

#include <gtest/gtest.h>

#include <vector>

namespace
{
    struct Frame
    {
        std::vector<int> Shown;
        std::vector<int> ExpectRendered;
    };

    // One frame is the render side advancing, then the UI marking what it drew.
    void RunFrames(DisplayLedger<int>& ledger, const std::vector<Frame>& frames, const std::vector<int>& keys)
    {
        for (std::size_t f = 0; f < frames.size(); ++f)
        {
            ledger.Advance();
            std::vector<int> rendered;
            for (const int key : keys)
                if (ledger.WasDisplayed(key))
                    rendered.push_back(key);
            EXPECT_EQ(rendered, frames[f].ExpectRendered) << "frame " << f;
            for (const int key : frames[f].Shown)
                ledger.MarkDisplayed(key);
        }
    }
}

TEST(DisplayLedger, RendersOnlyWhatTheUiShowedLastFrame)
{
    DisplayLedger<int> ledger;
    RunFrames(ledger,
        {
            { .Shown = { 1 }, .ExpectRendered = {} },
            { .Shown = { 1, 2 }, .ExpectRendered = { 1 } },
            { .Shown = { 2 }, .ExpectRendered = { 1, 2 } },
            // Hidden: released the next frame, whatever hid it.
            { .Shown = {}, .ExpectRendered = { 2 } },
            { .Shown = {}, .ExpectRendered = {} },
            // Shown again: rendered from the frame after.
            { .Shown = { 1 }, .ExpectRendered = {} },
            { .Shown = { 1 }, .ExpectRendered = { 1 } },
        },
        { 1, 2 });
}

TEST(DisplayLedger, ShowingTwiceInOneFrameIsOneEntry)
{
    DisplayLedger<int> ledger;
    ledger.MarkDisplayed(7);
    ledger.MarkDisplayed(7);
    ledger.Advance();
    EXPECT_EQ(ledger.Displayed().size(), 1u);
}

TEST(DisplayLedger, ForgottenKeysAreNotDisplayed)
{
    DisplayLedger<int> ledger;
    ledger.MarkDisplayed(3);
    ledger.Advance();
    ledger.MarkDisplayed(3);
    ledger.Forget(3);
    EXPECT_FALSE(ledger.WasDisplayed(3));
    ledger.Advance();
    EXPECT_FALSE(ledger.WasDisplayed(3));
}
