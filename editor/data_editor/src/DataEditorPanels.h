#pragma once

#include "ui/IEditorPanel.h"

#include <array>
#include <cstddef>
#include <string>

class DataEditorWorkspace;

class DataAssetBrowserPanel final : public IEditorPanel
{
public:
    explicit DataAssetBrowserPanel(DataEditorWorkspace& workspace);
    [[nodiscard]] std::string_view GetTitle() const override { return "Data Assets"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Left; }
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
    explicit DataFormPanel(DataEditorWorkspace& workspace);
    [[nodiscard]] std::string_view GetTitle() const override { return "Data"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Center; }
    void OnDraw() override;

private:
    DataEditorWorkspace& Workspace;
};

class DataDocumentationPanel final : public IEditorPanel
{
public:
    explicit DataDocumentationPanel(DataEditorWorkspace& workspace);
    [[nodiscard]] std::string_view GetTitle() const override { return "Documentation"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Right; }
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
    void OnDraw() override;

private:
    void Refresh();

    DataEditorWorkspace& Workspace;
    std::string LoadedPath;
    std::array<char, 65536> Buffer{};
    std::string ParseError;
};
