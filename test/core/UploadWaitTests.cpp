#include <gtest/gtest.h>

#include <graphics/vulkan/VulkanUploadContextService.h>

TEST(UploadWait, ACompletedUploadFreesItsCommandBuffer)
{
    const UploadWaitResponse response = ClassifyUploadWait(VK_SUCCESS);

    EXPECT_TRUE(response.FreeCommandBuffer);
    EXPECT_FALSE(response.Wedged);
}

TEST(UploadWait, ATimeoutKeepsEverythingThePendingSubmissionReads)
{
    const UploadWaitResponse response = ClassifyUploadWait(VK_TIMEOUT);

    EXPECT_FALSE(response.FreeCommandBuffer);
    EXPECT_TRUE(response.Wedged);
}

TEST(UploadWait, DeviceLossRetiresTheSubmission)
{
    const UploadWaitResponse response = ClassifyUploadWait(VK_ERROR_DEVICE_LOST);

    EXPECT_TRUE(response.FreeCommandBuffer);
    EXPECT_FALSE(response.Wedged);
}
