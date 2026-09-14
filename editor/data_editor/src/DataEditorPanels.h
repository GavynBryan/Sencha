#pragma once

#include "ui/IEditorPanel.h"

#include <array>
#include <cstddef>
#include <string>

#include <cstdint>

class DataEditorWorkspace;
class SubtypeEditorRegistry;

class DataAssetBrowserPanel final : public IEditorPanel
{
public:
    explicit DataAssetBrowserPanel(DataEditorWorkspace& workspace);
    [[nodiscard]] std::string_view GetTitle() const override { return "Data Assets"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Left; }
    [[nodiscard]] PanelPersistence GetPersistence() const override { return { "data_assets", PanelVisibilityPolicy::Remembered }; }
    void OnDraw() override;

private:
    DataEditorWorkspace& Workspace;
    int SelectedSubtype = 0;
    std::string SelectedAsset;
    std::array<char, 512> NewPath{};
    std::array<char, 512> OperationPath{};
    std::string LastError;
};

class DataFormPanel final : public IEditorPanel
{
public:
    DataFormPanel(DataEditorWorkspace& workspace, SubtypeEditorRegistry& editors);
    [[nodiscard]] std::string_view GetTitle() const override { return "Data"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Center; }
    [[nodiscard]] PanelPersistence GetPersistence() const override { return { "data", PanelVisibilityPolicy::SessionOnly }; }
    void OnDraw() override;

private:
    DataEditorWorkspace& Workspace;
    SubtypeEditorRegistry& Editors;
};

class DataDocumentationPanel final : public IEditorPanel
{
public:
    explicit DataDocumentationPanel(DataEditorWorkspace& workspace);
    [[nodiscard]] std::string_view GetTitle() const override { return "Documentation"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Right; }
    [[nodiscard]] PanelPersistence GetPersistence() const override { return { "documentation", PanelVisibilityPolicy::Remembered }; }
    void OnDraw() override;

private:
    DataEditorWorkspace& Workspace;
};

class DataValidationPanel final : public IEditorPanel
{
public:
    explicit DataValidationPanel(DataEditorWorkspace& workspace);
    [[nodiscard]] std::string_view GetTitle() const override { return "Validation"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Bottom; }
    [[nodiscard]] PanelPersistence GetPersistence() const override { return { "validation", PanelVisibilityPolicy::Remembered }; }
    void OnDraw() override;

private:
    DataEditorWorkspace& Workspace;
};

class DataRawJsonPanel final : public IEditorPanel
{
public:
    explicit DataRawJsonPanel(DataEditorWorkspace& workspace);
    [[nodiscard]] std::string_view GetTitle() const override { return "Raw JSON"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::CenterBottom; }
    [[nodiscard]] PanelPersistence GetPersistence() const override { return { "raw_json", PanelVisibilityPolicy::Remembered }; }
    void OnDraw() override;

private:
    void Refresh();

    DataEditorWorkspace& Workspace;
    std::string LoadedPath;
    uint64_t LoadedRevision = 0;
    std::array<char, 65536> Buffer{};
    std::string ParseError;
};
