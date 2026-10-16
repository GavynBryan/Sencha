#include <gtest/gtest.h>

#include <graphics/SwapchainImageLedger.h>

TEST(SwapchainImageLedger, AnImageIsHeldFromAcquireToPresentThenPresentedUntilAcquiredAgain)
{
    SwapchainImageLedger ledger;
    ledger.Reset(4);
    EXPECT_EQ(ledger.Held(), 0u);
    EXPECT_EQ(ledger.PresentedNotReacquired(), 0u);

    EXPECT_EQ(ledger.Acquired(2), 0u) << "never presented";
    EXPECT_EQ(ledger.Held(), 1u);
    ledger.Presented(2, 7);
    EXPECT_EQ(ledger.Held(), 0u);
    EXPECT_EQ(ledger.PresentedNotReacquired(), 1u);

    EXPECT_EQ(ledger.Acquired(1), 0u);
    ledger.Presented(1, 8);
    EXPECT_EQ(ledger.PresentedNotReacquired(), 2u);

    EXPECT_EQ(ledger.Acquired(2), 7u) << "the present the image last carried";
    EXPECT_EQ(ledger.Held(), 1u);
    EXPECT_EQ(ledger.PresentedNotReacquired(), 1u);
}

TEST(SwapchainImageLedger, ARebuiltChainStartsOver)
{
    SwapchainImageLedger ledger;
    ledger.Reset(3);
    (void)ledger.Acquired(0);
    ledger.Presented(0, 5);
    (void)ledger.Acquired(1);

    ledger.Reset(4);
    EXPECT_EQ(ledger.Held(), 0u);
    EXPECT_EQ(ledger.PresentedNotReacquired(), 0u);
    EXPECT_EQ(ledger.Acquired(0), 0u);
}

TEST(SwapchainImageLedger, AnIndexOutsideTheChainChangesNothing)
{
    SwapchainImageLedger ledger;
    ledger.Reset(2);
    EXPECT_EQ(ledger.Acquired(2), 0u);
    ledger.Presented(5, 9);
    EXPECT_EQ(ledger.Held(), 0u);
    EXPECT_EQ(ledger.PresentedNotReacquired(), 0u);
}
