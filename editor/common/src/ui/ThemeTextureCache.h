#pragma once

#include <graphics/GpuFrameRetirement.h>
#include <graphics/ImageHandle.h>

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

class Logger;
class VulkanImageService;
class VulkanSamplerCache;

//=============================================================================
// ThemeTextureCache: the raster art a theme owns.
//
// A theme names artwork by path; this turns those paths into draw-ready images
// and owns them. It is deliberately not the font atlas: fonts, icons and the
// shell's mark are baked once and rebuilt only when *their* inputs change,
// while theme artwork comes and goes with whichever theme is loaded. Keeping
// the two apart is what lets a theme switch cost one upload instead of a font
// atlas rebuild, and what lets a future theme add panel or bezel art without
// the font system learning anything about it.
//
// Identity is the path *and* its source stamp, so editing a file in place and
// reloading replaces the resident image even though the path did not change. A
// path that fails to load is remembered as failed, stamp and all, so a broken
// theme does not retry every frame and a repaired one recovers without a
// restart.
//
// Resolution is a preparation step, never a draw step: Prepare() is the only
// call that touches the filesystem, allocates, or uploads, and Get() is a
// lookup. Painting must never reach a loader.
//
// Lifetime follows the editor's existing conventions. Images go through
// VulkanImageService, whose Destroy is already deferred behind the deletion
// queue; ImGui descriptor sets are freed immediately by the backend, so they
// wait on GpuFrameRetirement the way ImGuiTargetPresenter's do. Shutdown() is
// the exception: teardown has no future frames to retire against, so it frees
// outright and must run with the device idle and the ImGui Vulkan backend
// still alive.
//=============================================================================
class ThemeTextureCache
{
public:
    ThemeTextureCache(VulkanImageService& images, VulkanSamplerCache& samplers, Logger* log);
    ~ThemeTextureCache();

    ThemeTextureCache(const ThemeTextureCache&) = delete;
    ThemeTextureCache& operator=(const ThemeTextureCache&) = delete;

    // A resolved image, ready to draw. A zero id means "not resident": the
    // caller falls back rather than asking again.
    struct Texture
    {
        ImTextureID Id = 0;
        ImVec2 Size{};
    };

    // Frees sets the GPU has proven done with. Once per UI frame, before Prepare.
    void BeginFrame(GpuFrameRetirement retirement);

    // Brings the cache in line with the set of paths the current style state
    // asks for: loads what is new or changed on disk, drops what is no longer
    // requested. The only call here that does I/O or GPU work.
    void Prepare(std::span<const std::string> paths);

    // The resident image for `path`, or a zero Texture. Never loads.
    [[nodiscard]] Texture Get(std::string_view path) const;

    // Frees every image and descriptor set outright. The device must be idle
    // and the ImGui Vulkan backend alive.
    void Shutdown();

    // What this cache has actually done, so a theme switch can be shown to
    // invalidate only what it should.
    struct Counters
    {
        std::uint32_t Loads = 0;        // successful decodes + uploads
        std::uint32_t Replacements = 0; // a resident image swapped for a newer source
        std::uint32_t Failures = 0;     // missing or undecodable sources
        std::size_t Resident = 0;
    };
    [[nodiscard]] Counters Stats() const;

    // What makes a file "the same file": a path alone is not an identity, since
    // editing art in place and reloading must replace the resident image. Cheap
    // enough to poll, and the same pair the editor's other source watchers
    // compare. Public because it is the rule this cache's correctness rests on,
    // and a rule worth a test.
    struct SourceStamp
    {
        std::filesystem::file_time_type Mtime{};
        std::uintmax_t Size = 0;
        bool Exists = false;

        bool operator==(const SourceStamp&) const = default;
    };
    [[nodiscard]] static SourceStamp StampOf(const std::string& path);

private:

    struct Entry
    {
        std::string Path;
        SourceStamp Stamp;
        ImageHandle Image;
        VkDescriptorSet Set = VK_NULL_HANDLE;
        ImVec2 Size{};
        bool Failed = false;
        std::uint64_t LastRequested = 0;
    };

    struct RetiredSet
    {
        VkDescriptorSet Set = VK_NULL_HANDLE;
        std::uint64_t Stamp = 0;
    };

    // True when this entry's source should be re-examined this frame: a new
    // entry always, a resident one only on the poll interval.
    [[nodiscard]] bool ShouldRestat(const Entry& entry) const;
    bool Load(Entry& entry);
    void ReleaseResources(Entry& entry, bool immediate);
    void Retire(VkDescriptorSet set);
    void FlushRetired(bool force);

    VulkanImageService& Images;
    VulkanSamplerCache& Samplers;
    Logger* Log = nullptr;

    std::vector<Entry> Entries;
    std::vector<RetiredSet> Retired;
    GpuFrameRetirement Retirement;
    std::uint64_t Frame = 0;
    Counters Stat;
};
