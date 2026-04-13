#include <window/SdlWindowService.h>

#include <window/SdlVideoService.h>
#include <window/SdlWindow.h>
#include <window/WindowCreateInfo.h>

#include <SDL3/SDL_vulkan.h>
#include <algorithm>

SdlWindowService::SdlWindowService(LoggingProvider& logging, SdlVideoService& video)
    : Log(logging.GetLogger<SdlWindowService>())
    , Logging(logging)
    , Video(video)
{
}

SdlWindowService::~SdlWindowService() = default;

SdlWindow* SdlWindowService::CreateWindow(const WindowCreateInfo& createInfo)
{
    auto window = std::make_unique<SdlWindow>(Logging, Video, createInfo);
    if (!window->IsValid())
    {
        return nullptr;
    }

    WindowRecord record;
    record.Id = window->GetId();
    record.Window = std::move(window);
    RefreshState(record);

    if (PrimaryWindowId == 0)
    {
        PrimaryWindowId = record.Id;
    }

    auto* result = record.Window.get();
    Windows.push_back(std::move(record));
    return result;
}

SdlWindow* SdlWindowService::GetWindow(WindowId id) const
{
    auto* record = FindRecord(id);
    return record ? record->Window.get() : nullptr;
}

SdlWindow* SdlWindowService::GetPrimaryWindow() const
{
    return GetWindow(PrimaryWindowId);
}

SDL_Window* SdlWindowService::GetNativeHandle(WindowId id) const
{
    auto* window = GetWindow(id);
    return window ? window->GetHandle() : nullptr;
}

std::vector<const char*> SdlWindowService::GetRequiredVulkanInstanceExtensions() const
{
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!names || count == 0)
    {
        Log.Error("Failed to query SDL Vulkan extensions: {}", SDL_GetError());
        return {};
    }

    return { names, names + count };
}

std::vector<SdlWindowService::WindowId> SdlWindowService::GetWindowIds() const
{
    std::vector<WindowId> ids;
    ids.reserve(Windows.size());
    for (const auto& record : Windows)
    {
        ids.push_back(record.Id);
    }
    return ids;
}

bool SdlWindowService::HasLiveWindows() const
{
    return std::any_of(Windows.begin(), Windows.end(),
        [](const WindowRecord& record)
        {
            return record.Window && record.Window->IsValid() && !record.State.CloseRequested;
        });
}

const SdlWindowService::WindowState* SdlWindowService::GetState(WindowId id) const
{
    auto* record = FindRecord(id);
    return record ? &record->State : nullptr;
}

WindowExtent SdlWindowService::GetExtent(WindowId id) const
{
    auto* record = FindRecord(id);
    return record ? record->State.Extent : WindowExtent{};
}

bool SdlWindowService::IsCloseRequested(WindowId id) const
{
    auto* record = FindRecord(id);
    return record ? record->State.CloseRequested : true;
}

bool SdlWindowService::IsAlive(WindowId id) const
{
    auto* record = FindRecord(id);
    return record && record->Window && record->Window->IsValid() && !record->State.CloseRequested;
}

void SdlWindowService::RequestClose(WindowId id)
{
    if (auto* record = FindRecord(id))
    {
        record->State.CloseRequested = true;
    }
}

void SdlWindowService::HandleEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_QUIT)
    {
        for (auto& record : Windows)
        {
            record.State.CloseRequested = true;
        }
        return;
    }

    if (event.type < SDL_EVENT_WINDOW_FIRST || event.type > SDL_EVENT_WINDOW_LAST)
    {
        return;
    }

    auto* record = FindRecord(event.window.windowID);
    if (!record)
    {
        return;
    }

    switch (event.type)
    {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        record->State.CloseRequested = true;
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
        record->State.Minimized = true;
        break;
    case SDL_EVENT_WINDOW_RESTORED:
        record->State.Minimized = false;
        RefreshState(*record);
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        record->State.Focused = true;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        record->State.Focused = false;
        break;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        RefreshState(*record);
        record->State.ResizePending = true;
        break;
    default:
        break;
    }
}

bool SdlWindowService::ConsumeResize(WindowId id, WindowExtent* extent)
{
    auto* record = FindRecord(id);
    if (!record || !record->State.ResizePending)
    {
        return false;
    }

    record->State.ResizePending = false;
    if (extent)
    {
        *extent = record->State.Extent;
    }
    return true;
}

SdlWindowService::WindowRecord* SdlWindowService::FindRecord(WindowId id)
{
    auto iter = std::find_if(Windows.begin(), Windows.end(),
        [id](const WindowRecord& record)
        {
            return record.Id == id;
        });

    return iter == Windows.end() ? nullptr : &*iter;
}

const SdlWindowService::WindowRecord* SdlWindowService::FindRecord(WindowId id) const
{
    auto iter = std::find_if(Windows.begin(), Windows.end(),
        [id](const WindowRecord& record)
        {
            return record.Id == id;
        });

    return iter == Windows.end() ? nullptr : &*iter;
}

void SdlWindowService::RefreshState(WindowRecord& record)
{
    if (!record.Window || !record.Window->IsValid())
    {
        record.State.Extent = {};
        record.State.Focused = false;
        record.State.Minimized = true;
        return;
    }

    record.State.Extent = record.Window->GetExtent();
    auto flags = SDL_GetWindowFlags(record.Window->GetHandle());
    record.State.Minimized = (flags & SDL_WINDOW_MINIMIZED) != 0;
    record.State.Focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
}
