#include "OutlinePanel.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"

#include <imgui.h>

#include <string>

namespace
{
    std::string NodeLabel(const UiElementInfo& info)
    {
        std::string label = info.Tag;
        if (!info.Id.empty())
        {
            label += '#';
            label += info.Id;
        }
        for (const std::string& cls : info.Classes)
        {
            label += '.';
            label += cls;
        }
        return label;
    }
}

OutlinePanel::OutlinePanel(UiPreviewSession& session, PreviewViewState& view)
    : Session(session)
    , View(view)
{
}

void OutlinePanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible, PanelStyle::Standard);
    if (!panel.IsOpen())
        return;
    if (!Session.IsOpen())
    {
        ImGui::TextDisabled("No document open.");
        return;
    }
    // A preorder snapshot; children follow their parent, so the walk below
    // consumes the vector once. Per frame, proportional to the tree.
    const std::vector<UiElementInfo> tree = Session.Service().ElementTree(Session.OpenScreen());
    if (tree.empty())
    {
        ImGui::TextDisabled("Nothing laid out yet.");
        return;
    }
    HoveredThisFrame = false;
    DrawNode(tree, 0);
    // Hover from this panel is transient: leaving every row clears it, and
    // the Preview's own hover takes over when the pointer is there instead.
    if (!HoveredThisFrame && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)
        && Session.Mode() == UiPreviewSession::PointerMode::Inspect)
        View.Hovered = {};
}

void OutlinePanel::DrawNode(const std::vector<UiElementInfo>& tree, std::size_t index)
{
    const UiElementInfo& node = tree[index];
    // Children are the following entries one level deeper, up to the next
    // entry at this depth or shallower.
    std::size_t end = index + 1;
    while (end < tree.size() && tree[end].Depth > node.Depth)
        ++end;
    const bool leaf = end == index + 1;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
                               | ImGuiTreeNodeFlags_DefaultOpen;
    if (leaf)
        flags |= ImGuiTreeNodeFlags_Leaf;
    if (node.Ref == View.Selected)
        flags |= ImGuiTreeNodeFlags_Selected;

    const std::string label = NodeLabel(node);
    ImGui::PushID(static_cast<int>(node.Ref.Slot));
    const bool open = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
    if (ImGui::IsItemHovered())
    {
        View.Hovered = node.Ref;
        HoveredThisFrame = true;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
        View.Selected = node.Ref;
    if (open)
    {
        for (std::size_t child = index + 1; child < end;)
        {
            if (tree[child].Depth == node.Depth + 1)
                DrawNode(tree, child);
            // Skip this child's subtree.
            std::size_t next = child + 1;
            while (next < end && tree[next].Depth > tree[child].Depth)
                ++next;
            child = next;
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}
