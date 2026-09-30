#include <graphics/vulkan/VulkanFrameService.h>

#include <graphics/vulkan/PresentationFramePlan.h>
#include <graphics/vulkan/PresentationTarget.h>
#include <graphics/vulkan/VulkanDeletionQueueService.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#include <graphics/vulkan/VulkanQueueService.h>
#include <graphics/vulkan/VulkanSurfaceService.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <platform/SdlWindow.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

namespace
{
    using ProfileClock = std::chrono::steady_clock;

    double SecondsSince(ProfileClock::time_point start)
    {
        return std::chrono::duration<double>(ProfileClock::now() - start).count();
    }

    VulkanFrameStatus FatalStatus(SurfaceOutcome outcome)
    {
        return outcome == SurfaceOutcome::DeviceLost ? VulkanFrameStatus::DeviceLost : VulkanFrameStatus::Error;
    }
}

VulkanFrameService::VulkanFrameService(const Services& services,
                                       std::unique_ptr<VulkanSurfaceService> primarySurface,
                                       SdlWindow& primaryWindow, uint32_t framesInFlight)
    : Log(services.Logging->GetLogger<VulkanFrameService>())
    , Deps(services)
    , Device(services.Device->GetDevice())
{
    if (!services.Device->IsValid())
    {
        Log.Error("Cannot create Vulkan frame service: VulkanDeviceService is not valid");
        return;
    }

    const VulkanQueueService& queues = *services.Queues;
    if (!queues.IsValid() || !queues.GetQueueFamilies().HasGraphics()
        || queues.GetGraphicsQueue() == VK_NULL_HANDLE)
    {
        Log.Error("Cannot create Vulkan frame service: graphics queue is required");
        return;
    }

    if (!CreateFrameData(std::max(framesInFlight, 1u)))
        return;

    std::unique_ptr<PresentationTarget> primary = MakeTarget(std::move(primarySurface), primaryWindow, PresentationDesc{});
    if (primary == nullptr)
    {
        Log.Error("Cannot create Vulkan frame service: the primary window cannot be presented to");
        return;
    }
    Primary = Presentations.Adopt(std::move(primary));

    // Resolve VK_KHR_present_wait if the device picked it up. This is the
    // only reliable way to block the CPU on a specific vsync — without it
    // we fall back to acquire-based pacing, which desynchronizes on drivers
    // that queue up multiple images.
    for (const auto* ext : services.Device->GetEnabledDeviceExtensions())
    {
        if (ext && std::strcmp(ext, VK_KHR_PRESENT_WAIT_EXTENSION_NAME) == 0)
        {
            PresentWaitEnabled = true;
            break;
        }
    }

    if (PresentWaitEnabled)
    {
        WaitForPresentFn = reinterpret_cast<PFN_vkWaitForPresentKHR>(
            vkGetDeviceProcAddr(Device, "vkWaitForPresentKHR"));
        if (!WaitForPresentFn)
        {
            Log.Warn("VK_KHR_present_wait enabled but vkWaitForPresentKHR unresolved");
            PresentWaitEnabled = false;
        }
    }

    Valid = true;
    Log.Info("Vulkan frame service created: frames in flight {} present_wait {}",
             Frames.size(), PresentWaitEnabled ? "on" : "off");
}

VulkanFrameService::~VulkanFrameService()
{
    DestroyFrameData();
}

std::unique_ptr<PresentationTarget> VulkanFrameService::MakeTarget(std::unique_ptr<VulkanSurfaceService> surface,
                                                                   SdlWindow& window, const PresentationDesc& desc)
{
    const PresentationTarget::Services targetServices{
        .Logging = Deps.Logging,
        .Device = Deps.Device,
        .PhysicalDevice = Deps.PhysicalDevice,
        .Queues = Deps.Queues,
        .Images = Deps.Images,
    };
    auto target = std::make_unique<PresentationTarget>(targetServices, std::move(surface), window, desc,
                                                       static_cast<uint32_t>(Frames.size()));
    return target->IsValid() ? std::move(target) : nullptr;
}

VulkanSwapchainService& VulkanFrameService::PrimarySwapchain() const
{
    return const_cast<HandlePool<PresentationIdTag, PresentationTarget>&>(Presentations).Find(Primary)->Swapchain();
}

PresentationTarget* VulkanFrameService::FindPresentation(PresentationId id)
{
    return Presentations.Find(id);
}

PresentationId VulkanFrameService::CreatePresentation(SdlWindow& window, const PresentationDesc& desc)
{
    if (!Valid)
        return {};
    auto surface = std::make_unique<VulkanSurfaceService>(*Deps.Logging, *Deps.Instance, window);
    std::unique_ptr<PresentationTarget> target = MakeTarget(std::move(surface), window, desc);
    if (target == nullptr)
        return {};
    return Presentations.Adopt(std::move(target));
}

bool VulkanFrameService::RetirePresentation(PresentationId id, std::function<void()> afterRetired)
{
    if (id == Primary)
        return false;
    std::unique_ptr<PresentationTarget> target = Presentations.Release(id);
    if (target == nullptr)
        return false;
    // One frame past the current: a frame's fence proves its commands done,
    // not the presentation queued after them, so the chain waits for the
    // next submission on the same queue to complete as well.
    Retiring.push_back(Retiree{
        .Target = std::move(target),
        .Stamp = Retirement.Stamp() + 1,
        .AfterRetired = std::move(afterRetired),
    });
    return true;
}

bool VulkanFrameService::RebuildPresentation(PresentationId id, WindowExtent extent)
{
    PresentationTarget* target = Presentations.Find(id);
    return target != nullptr && target->Rebuild(extent);
}

void VulkanFrameService::RebuildStaleSecondaries()
{
    Presentations.ForEach([this](PresentationId id, PresentationTarget& target) {
        if (id == Primary)
            return;
        const PresentationAvailability availability = target.Availability();
        if (availability == PresentationAvailability::NeedsRebuild
            || (availability == PresentationAvailability::Acquirable && target.ExtentChanged()))
            target.Rebuild(target.Window().GetExtent());
    });
}

bool VulkanFrameService::AnySecondaryVisible()
{
    bool visible = false;
    Presentations.ForEach([&](PresentationId id, PresentationTarget& target) {
        const PresentationAvailability availability = target.Availability();
        visible |= id != Primary && availability != PresentationAvailability::Minimized
            && availability != PresentationAvailability::ZeroExtent;
    });
    return visible;
}

void VulkanFrameService::DestroyRetiredPresentations()
{
    // Out of the list before the callbacks run, so one that retires another
    // presentation does not reenter a half-walked vector.
    std::vector<Retiree> retired;
    for (auto it = Retiring.begin(); it != Retiring.end();)
    {
        if (!Retirement.IsRetired(it->Stamp))
        {
            ++it;
            continue;
        }
        retired.push_back(std::move(*it));
        it = Retiring.erase(it);
    }
    for (Retiree& retiree : retired)
    {
        retiree.Target.reset();
        if (retiree.AfterRetired)
            retiree.AfterRetired();
    }
}

void VulkanFrameService::WaitForPrimaryPresent()
{
    // Block on the prior cycle's presentation of this frame slot. This is the
    // true vsync anchor — without it the GPU can queue ahead, making frame
    // cadence lumpy. Skip when the presentId belongs to a retired swapchain
    // (vkWaitForPresentKHR on a dead swapchain is undefined).
    PresentationTarget* primary = Presentations.Find(Primary);
    if (!PresentWaitEnabled || WaitForPresentFn == nullptr || primary == nullptr)
        return;
    PresentationTarget::PresentRecord& last = primary->LastPresent(CurrentFrame);
    if (last.Id == 0 || last.SwapchainGeneration != primary->Swapchain().GetGeneration())
        return;
    const auto waitStart = ProfileClock::now();
    const VkResult result = WaitForPresentFn(Device, primary->Swapchain().GetSwapchain(), last.Id,
                                             std::numeric_limits<uint64_t>::max());
    LastTiming.PresentWaitSeconds = SecondsSince(waitStart);
    // OUT_OF_DATE and SUBOPTIMAL are benign here; the acquire surfaces them.
    if (result == VK_ERROR_DEVICE_LOST)
        Log.Error("vkWaitForPresentKHR failed: device lost");
    last.Id = 0;
}

VulkanFrameStatus VulkanFrameService::AcquirePresentations(VulkanFrame& frame)
{
    VulkanFrameStatus status = VulkanFrameStatus::Ready;
    Presentations.ForEach([&](PresentationId id, PresentationTarget& target) {
        if (status != VulkanFrameStatus::Ready || target.Availability() != PresentationAvailability::Acquirable)
            return;

        uint32_t imageIndex = 0;
        const auto acquireStart = ProfileClock::now();
        const VkResult result = vkAcquireNextImageKHR(Device, target.Swapchain().GetSwapchain(),
                                                      std::numeric_limits<uint64_t>::max(),
                                                      target.AcquireSemaphore(CurrentFrame), VK_NULL_HANDLE,
                                                      &imageIndex);
        LastTiming.AcquireSeconds += SecondsSince(acquireStart);
        const SurfaceOutcome outcome = ClassifySurfaceResult(result);
        if (id == Primary)
            frame.PrimaryAcquire = outcome;
        if (IsFatal(outcome))
        {
            Log.Error("vkAcquireNextImageKHR failed with code {}", static_cast<int>(result));
            status = FatalStatus(outcome);
            return;
        }
        if (outcome == SurfaceOutcome::OutOfDate)
        {
            target.MarkNeedsRebuild();
            return;
        }

        PresentationTarget::ImageState* image = target.Image(imageIndex);
        if (image == nullptr)
        {
            Log.Error("Acquired swapchain image index is outside the presentation's image state");
            status = VulkanFrameStatus::Error;
            return;
        }
        if (image->InFlightFence != VK_NULL_HANDLE)
        {
            const VkResult waitResult = vkWaitForFences(Device, 1, &image->InFlightFence, VK_TRUE,
                                                        std::numeric_limits<uint64_t>::max());
            if (waitResult != VK_SUCCESS)
            {
                Log.Error("Waiting for swapchain image fence failed with code {}", static_cast<int>(waitResult));
                status = waitResult == VK_ERROR_DEVICE_LOST ? VulkanFrameStatus::DeviceLost
                                                            : VulkanFrameStatus::Error;
                return;
            }
        }

        const VulkanSwapchainService& chain = target.Swapchain();
        Acquired.push_back(AcquiredPresentation{
            .Id = id,
            .Target = &target,
            .ImageIndex = imageIndex,
            .Image = chain.GetImage(imageIndex),
            .View = chain.GetImageView(imageIndex),
            .Format = chain.GetFormat(),
            .Extent = chain.GetExtent(),
            .Suboptimal = outcome == SurfaceOutcome::Suboptimal,
        });
        if (id == Primary)
        {
            LastTiming.ImageIndex = imageIndex;
            LastTiming.SwapchainGeneration = chain.GetGeneration();
        }
    });
    return status;
}

VulkanFrameStatus VulkanFrameService::BeginFrame(VulkanFrame& frame)
{
    frame = {};
    Acquired.clear();
    LastTiming.AcquireSeconds = 0.0;
    LastTiming.PresentWaitSeconds = 0.0;
    LastTiming.ImageIndex = 0;

    if (!Valid)
        return VulkanFrameStatus::Error;

    FrameData& current = Frames[CurrentFrame];
    if (current.Submitted)
    {
        const VkResult waitResult = vkWaitForFences(Device, 1, &current.InFlightFence, VK_TRUE,
                                                    std::numeric_limits<uint64_t>::max());
        if (waitResult == VK_ERROR_DEVICE_LOST)
        {
            Log.Error("vkWaitForFences failed: device lost");
            return VulkanFrameStatus::DeviceLost;
        }
        if (waitResult != VK_SUCCESS)
        {
            Log.Error("vkWaitForFences failed with code {}", static_cast<int>(waitResult));
            return VulkanFrameStatus::Error;
        }

        current.Submitted = false;
        // The wait just proved this slot's last frame complete. That frame is
        // the oldest one outstanding -- slots are used strictly round-robin --
        // so everything numbered at or below it is now safe to release.
        Retirement.RetiredThrough = AdvanceRetiredThrough(
            Retirement.RetiredThrough, current.SubmittedFrameNumber);
        Deps.DeletionQueue->AdvanceFrame();
    }
    DestroyRetiredPresentations();
    WaitForPrimaryPresent();

    const VulkanFrameStatus acquired = AcquirePresentations(frame);
    if (acquired != VulkanFrameStatus::Ready)
        return acquired;
    if (Acquired.empty())
        return VulkanFrameStatus::NothingAcquired;

    const VkResult resetPoolResult = vkResetCommandPool(Device, current.CommandPool, 0);
    if (resetPoolResult != VK_SUCCESS)
    {
        Log.Error("vkResetCommandPool failed with code {}", static_cast<int>(resetPoolResult));
        return resetPoolResult == VK_ERROR_DEVICE_LOST ? VulkanFrameStatus::DeviceLost : VulkanFrameStatus::Error;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    const VkResult beginResult = vkBeginCommandBuffer(current.CommandBuffer, &beginInfo);
    if (beginResult != VK_SUCCESS)
    {
        Log.Error("vkBeginCommandBuffer failed with code {}", static_cast<int>(beginResult));
        return beginResult == VK_ERROR_DEVICE_LOST ? VulkanFrameStatus::DeviceLost : VulkanFrameStatus::Error;
    }

    // Numbered only once the frame is certain to record: an acquire that bails
    // out above must not consume a number, or the retirement boundary would
    // trail a frame that never existed.
    ++Retirement.Current;
    frame.FrameIndex = CurrentFrame;
    frame.CommandBuffer = current.CommandBuffer;
    frame.Presentations = Acquired;
    return VulkanFrameStatus::Ready;
}

SurfaceOutcome VulkanFrameService::EndFrame(const VulkanFrame& frame)
{
    if (!Valid || frame.FrameIndex >= Frames.size())
        return SurfaceOutcome::Failed;

    FrameData& current = Frames[frame.FrameIndex];
    LastTiming.SubmitSeconds = 0.0;
    LastTiming.PresentSeconds = 0.0;

    const VkResult endResult = vkEndCommandBuffer(current.CommandBuffer);
    if (endResult != VK_SUCCESS)
    {
        Log.Error("vkEndCommandBuffer failed with code {}", static_cast<int>(endResult));
        return endResult == VK_ERROR_DEVICE_LOST ? SurfaceOutcome::DeviceLost : SurfaceOutcome::Failed;
    }

    WaitSemaphores.clear();
    WaitStages.clear();
    SignalSemaphores.clear();
    for (const AcquiredPresentation& presentation : frame.Presentations)
    {
        WaitSemaphores.push_back(presentation.Target->AcquireSemaphore(frame.FrameIndex));
        WaitStages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        SignalSemaphores.push_back(presentation.Target->Image(presentation.ImageIndex)->RenderFinished);
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = static_cast<uint32_t>(WaitSemaphores.size());
    submitInfo.pWaitSemaphores = WaitSemaphores.data();
    submitInfo.pWaitDstStageMask = WaitStages.data();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &current.CommandBuffer;
    submitInfo.signalSemaphoreCount = static_cast<uint32_t>(SignalSemaphores.size());
    submitInfo.pSignalSemaphores = SignalSemaphores.data();

    const VkResult resetFenceResult = vkResetFences(Device, 1, &current.InFlightFence);
    if (resetFenceResult != VK_SUCCESS)
    {
        Log.Error("vkResetFences failed with code {}", static_cast<int>(resetFenceResult));
        return resetFenceResult == VK_ERROR_DEVICE_LOST ? SurfaceOutcome::DeviceLost : SurfaceOutcome::Failed;
    }

    const auto submitStart = ProfileClock::now();
    const VkResult submitResult = vkQueueSubmit(Deps.Queues->GetGraphicsQueue(), 1, &submitInfo,
                                                current.InFlightFence);
    LastTiming.SubmitSeconds = SecondsSince(submitStart);
    if (submitResult != VK_SUCCESS)
    {
        current.Submitted = false;
        Log.Error("vkQueueSubmit failed with code {}", static_cast<int>(submitResult));
        return submitResult == VK_ERROR_DEVICE_LOST ? SurfaceOutcome::DeviceLost : SurfaceOutcome::Failed;
    }

    current.Submitted = true;
    current.SubmittedFrameNumber = Retirement.Current;

    // One present over every acquired swapchain; pResults keeps each one's
    // outcome its own.
    PresentSwapchains.clear();
    PresentImages.clear();
    PresentIds.clear();
    uint64_t primaryPresentId = 0;
    for (const AcquiredPresentation& presentation : frame.Presentations)
    {
        presentation.Target->Image(presentation.ImageIndex)->InFlightFence = current.InFlightFence;
        PresentSwapchains.push_back(presentation.Target->Swapchain().GetSwapchain());
        PresentImages.push_back(presentation.ImageIndex);
        const bool paced = PresentWaitEnabled && presentation.Id == Primary;
        if (paced)
            primaryPresentId = NextPresentId++;
        PresentIds.push_back(paced ? primaryPresentId : 0);
    }
    PresentResults.assign(PresentSwapchains.size(), VK_SUCCESS);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = static_cast<uint32_t>(SignalSemaphores.size());
    presentInfo.pWaitSemaphores = SignalSemaphores.data();
    presentInfo.swapchainCount = static_cast<uint32_t>(PresentSwapchains.size());
    presentInfo.pSwapchains = PresentSwapchains.data();
    presentInfo.pImageIndices = PresentImages.data();
    presentInfo.pResults = PresentResults.data();

    VkPresentIdKHR presentIdInfo{};
    if (PresentWaitEnabled)
    {
        presentIdInfo.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
        presentIdInfo.swapchainCount = static_cast<uint32_t>(PresentIds.size());
        presentIdInfo.pPresentIds = PresentIds.data();
        presentInfo.pNext = &presentIdInfo;
    }

    const auto presentStart = ProfileClock::now();
    const VkResult presentResult = vkQueuePresentKHR(Deps.Queues->GetPresentQueue(), &presentInfo);
    LastTiming.PresentSeconds = SecondsSince(presentStart);
    AdvanceFrame();

    if (presentResult == VK_ERROR_DEVICE_LOST)
    {
        Log.Error("vkQueuePresentKHR failed: device lost");
        return SurfaceOutcome::DeviceLost;
    }

    SurfaceOutcome primaryOutcome = frame.PrimaryAcquire;
    for (std::size_t i = 0; i < frame.Presentations.size(); ++i)
    {
        const AcquiredPresentation& presentation = frame.Presentations[i];
        SurfaceOutcome outcome = ClassifySurfaceResult(PresentResults[i]);
        if (outcome == SurfaceOutcome::Ok && presentation.Suboptimal)
            outcome = SurfaceOutcome::Suboptimal;
        if (outcome == SurfaceOutcome::Failed)
            Log.Error("vkQueuePresentKHR failed with code {}", static_cast<int>(PresentResults[i]));
        if (outcome == SurfaceOutcome::OutOfDate || outcome == SurfaceOutcome::Suboptimal)
            presentation.Target->MarkNeedsRebuild();

        if (presentation.Id != Primary)
            continue;
        primaryOutcome = outcome;
        // Only a presented frame has an id worth waiting on; an out-of-date
        // chain is retiring.
        PresentationTarget::PresentRecord& record = presentation.Target->LastPresent(frame.FrameIndex);
        const bool presented = outcome == SurfaceOutcome::Ok || outcome == SurfaceOutcome::Suboptimal;
        record.Id = presented ? primaryPresentId : 0;
        record.SwapchainGeneration = presentation.Target->Swapchain().GetGeneration();
    }
    return primaryOutcome;
}

bool VulkanFrameService::CreateFrameData(uint32_t framesInFlight)
{
    Frames.resize(framesInFlight);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    const uint32_t graphicsFamily = *Deps.Queues->GetQueueFamilies().Graphics;

    for (auto& frame : Frames)
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = graphicsFamily;

        VkResult poolResult = vkCreateCommandPool(Device, &poolInfo, nullptr, &frame.CommandPool);
        if (poolResult != VK_SUCCESS)
        {
            Log.Error("vkCreateCommandPool failed with code {}", static_cast<int>(poolResult));
            return false;
        }

        VkCommandBufferAllocateInfo commandInfo{};
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandInfo.commandPool = frame.CommandPool;
        commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = 1;

        VkResult commandResult = vkAllocateCommandBuffers(Device, &commandInfo, &frame.CommandBuffer);
        if (commandResult != VK_SUCCESS)
        {
            Log.Error("vkAllocateCommandBuffers failed with code {}", static_cast<int>(commandResult));
            return false;
        }

        if (vkCreateFence(Device, &fenceInfo, nullptr, &frame.InFlightFence) != VK_SUCCESS)
        {
            Log.Error("Failed to create Vulkan frame synchronization objects");
            return false;
        }
    }

    return true;
}

void VulkanFrameService::DestroyFrameData()
{
    if (Device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(Device);

    // Teardown runs before the windows are destroyed, and nothing is left to
    // tell about them, so retirees go without their callbacks.
    Retiring.clear();
    Presentations.Clear();

    for (auto& frame : Frames)
    {
        if (frame.InFlightFence != VK_NULL_HANDLE)
        {
            vkDestroyFence(Device, frame.InFlightFence, nullptr);
            frame.InFlightFence = VK_NULL_HANDLE;
        }

        if (frame.CommandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(Device, frame.CommandPool, nullptr);
            frame.CommandPool = VK_NULL_HANDLE;
            frame.CommandBuffer = VK_NULL_HANDLE;
        }
    }

    Frames.clear();
    Valid = false;
}

void VulkanFrameService::AdvanceFrame()
{
    CurrentFrame = (CurrentFrame + 1) % static_cast<uint32_t>(Frames.size());
}
