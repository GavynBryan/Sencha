// Which range the one frame UBO descriptor ends up carrying.
//
// Several passes bind set 0 binding 0 and each knows only its own block, so
// the range that gets written has to satisfy all of them. It used to be
// whichever pass wrote last, which made correctness rest on feature
// registration order -- the shadow pass declaring 64 bytes had to register
// before the forward pass declaring 5712, in the game and in the editor, each
// holding the rule in a comment. Reversing either would have left a 64-byte
// range under a shader declaring the larger block.

#include <gtest/gtest.h>

#include <graphics/FrameUniformRange.h>
#include <render/pass/MeshForwardPass.h>

#include <math/Mat.h>

TEST(FrameUniformRange, KeepsTheLargestDeclarationRegardlessOfOrder)
{
    // The two real declarations, in both orders. Neither may lose.
    const std::uint64_t shadow = sizeof(Mat4);
    const std::uint64_t forward = sizeof(MeshFrameUniforms);

    std::uint64_t shadowFirst = 0;
    shadowFirst = ResolveFrameUniformRange(shadowFirst, shadow);
    shadowFirst = ResolveFrameUniformRange(shadowFirst, forward);

    std::uint64_t forwardFirst = 0;
    forwardFirst = ResolveFrameUniformRange(forwardFirst, forward);
    forwardFirst = ResolveFrameUniformRange(forwardFirst, shadow);

    EXPECT_EQ(shadowFirst, forward);
    EXPECT_EQ(forwardFirst, forward);
}

TEST(FrameUniformRange, ASmallerDeclarationNeverShrinksTheDescriptor)
{
    EXPECT_EQ(ResolveFrameUniformRange(5712, 64), 5712u);
    EXPECT_EQ(ResolveFrameUniformRange(5712, 5712), 5712u);
    EXPECT_EQ(ResolveFrameUniformRange(5712, 6000), 6000u);
    EXPECT_EQ(ResolveFrameUniformRange(0, 0), 0u);
}

TEST(FrameUniformRange, TheForwardBlockIsInsideTheRecordedBudget)
{
    // The budget is a design line rather than a hardware one, so nothing
    // enforces it at runtime beyond a warning. This is what notices when a
    // frame-uniform field pushes the block past it.
    EXPECT_FALSE(FrameUniformRangeExceedsBudget(sizeof(MeshFrameUniforms)))
        << "MeshFrameUniforms is " << sizeof(MeshFrameUniforms) << " bytes against a "
        << kFrameUniformBudgetBytes << " byte budget; per-frame data this large belongs "
           "in a storage buffer";
    EXPECT_TRUE(FrameUniformRangeExceedsBudget(kFrameUniformBudgetBytes + 1));
    EXPECT_FALSE(FrameUniformRangeExceedsBudget(kFrameUniformBudgetBytes));
}
