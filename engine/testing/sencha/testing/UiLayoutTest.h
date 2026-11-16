#pragma once

#ifndef SENCHA_ENABLE_COOK
#error "UiLayoutTest cooks UI sources, which needs a build with the cook layer (SENCHA_ENABLE_COOK)"
#endif

#include <gtest/gtest.h>

#include <assets/cook/ContentImporters.h>
#include <assets/cook/ImportOnDemand.h>
#include <assets/runtime/ContentMount.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/logging/LoggingProvider.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

class UiTestContentRoot
{
public:
    UiTestContentRoot()
        : Root(std::filesystem::temp_directory_path()
               / ("sencha_ui_layout_" + std::to_string(std::random_device{}())))
    {
        std::filesystem::create_directories(Root);
    }
    ~UiTestContentRoot()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
    UiTestContentRoot(const UiTestContentRoot&) = delete;
    UiTestContentRoot& operator=(const UiTestContentRoot&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const { return Root; }

    void CopyIn(const std::filesystem::path& source, std::string_view into) const
    {
        std::filesystem::copy(source, Root / into,
                              std::filesystem::copy_options::recursive
                                  | std::filesystem::copy_options::overwrite_existing);
    }

    void Write(std::string_view relativePath, std::string_view text) const
    {
        const std::filesystem::path file = Root / relativePath;
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary | std::ios::trunc)
            .write(text.data(), static_cast<std::streamsize>(text.size()));
    }

private:
    std::filesystem::path Root;
};

class UiLayout
{
public:
    UiLayout()
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
        , Service(std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages, Assets.Fonts))
    {
    }
    ~UiLayout() { Service->Shutdown(); }
    UiLayout(const UiLayout&) = delete;
    UiLayout& operator=(const UiLayout&) = delete;

    [[nodiscard]] UiTestContentRoot& Content() { return Root; }

    [[nodiscard]] testing::AssertionResult Mount()
    {
        ContentImporterSet importers;
        const bool cooked = ImportAssetsOnDemand(Root.Path().string(), importers.Registry(), Assets.Registry, Logging);
        MountContentRoot(ResolveContentRoot(Root.Path()), Assets);
        Mounted = true;
        if (!cooked)
            return testing::AssertionFailure() << "a source under " << Root.Path() << " did not cook";
        return testing::AssertionSuccess();
    }

    UiScreenHandle Open(const UiScreenDesc& desc, RenderExtent extent, float scale = 1.0f,
                        UiSurfaceInputPolicy input = UiSurfaceInputPolicy::Full)
    {
        if (!Mounted)
        {
            EXPECT_TRUE(Mount());
        }
        EXPECT_TRUE(Service->IsReady());
        LastSurface = Service->CreateSurface("ui-layout-test", extent);
        Service->SetSurfaceScale(LastSurface, scale);
        Service->SetSurfaceInputPolicy(LastSurface, input);
        LastScreen = Service->OpenScreen(LastSurface, desc);
        EXPECT_TRUE(LastScreen.IsValid()) << desc.PackagePath << " did not open";
        return LastScreen;
    }

    UiScreenHandle OpenMarkup(std::string_view body, std::string_view style, RenderExtent extent,
                              UiScreenDesc desc = {})
    {
        Root.Write("ui/screen.rml",
                   std::string(R"(<rml><head><link type="text/rcss" href="/ui/base.rcss"/>)"
                               R"(<link type="text/rcss" href="screen.rcss"/></head><body)")
                       + (desc.ModelName.empty() ? std::string() : " data-model=\"" + desc.ModelName + "\"")
                       + ">" + std::string(body) + "</body></rml>");
        Root.Write("ui/screen.rcss", style);
        EXPECT_TRUE(Mount());
        desc.PackagePath = "asset://ui/screen.rml";
        return Open(desc, extent);
    }

    void Refresh()
    {
        Service->Update();
        Service->ExtractRender();
    }

    [[nodiscard]] std::optional<UiElementBox> BoxOf(std::string_view id, UiBoxArea area = UiBoxArea::Border) const
    {
        return Service->MeasureElement(LastScreen, id, area);
    }

    [[nodiscard]] std::vector<UiElementBox> BoxesWithClass(std::string_view className,
                                                           UiBoxArea area = UiBoxArea::Border)
    {
        std::vector<UiElementBox> found;
        for (const UiElementInfo& element : Service->ElementTree(LastScreen))
        {
            const UiElementBox box = BoxIn(element.Boxes, area);
            if (std::ranges::find(element.Classes, className) != element.Classes.end()
                && (box.Width > 0.0f || box.Height > 0.0f))
            {
                found.push_back(box);
            }
        }
        return found;
    }

    [[nodiscard]] bool Drew() const
    {
        return !Service->Frames().empty()
            || std::ranges::any_of(Service->TextureFrames(),
                                   [](const UiTextureFrame& frame) { return !frame.Frame.IsEmpty(); });
    }

    [[nodiscard]] testing::AssertionResult NoErrors()
    {
        std::string errors;
        for (const UiDiagnostic& diagnostic : Service->DrainDiagnostics())
            if (diagnostic.Severity == UiDiagnosticSeverity::Error)
                errors += "\n  " + diagnostic.Message;
        if (errors.empty())
            return testing::AssertionSuccess();
        return testing::AssertionFailure() << "the UI reported errors:" << errors;
    }

    [[nodiscard]] UiService& Ui() { return *Service; }
    [[nodiscard]] const UiService& Ui() const { return *Service; }
    [[nodiscard]] UiScreenHandle Screen() const { return LastScreen; }
    [[nodiscard]] UiSurfaceId Surface() const { return LastSurface; }

private:
    static UiElementBox BoxIn(const UiElementBoxes& boxes, UiBoxArea area)
    {
        switch (area)
        {
        case UiBoxArea::Content: return boxes.Content;
        case UiBoxArea::Padding: return boxes.Padding;
        case UiBoxArea::Border:  return boxes.Border;
        case UiBoxArea::Margin:  return boxes.Margin;
        }
        return boxes.Border;
    }

    UiTestContentRoot Root;
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Service;
    UiSurfaceId LastSurface;
    UiScreenHandle LastScreen;
    bool Mounted = false;
};

class UiLayoutTest : public testing::Test, public UiLayout
{
};
