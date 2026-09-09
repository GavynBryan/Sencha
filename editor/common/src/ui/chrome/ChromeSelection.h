#pragma once

// The one way a widget is styled as selected. Selection is amber and nothing
// else is: a global remap of ImGuiCol_Header would recolor every collapsing
// header and popup row along with the selected ones, so the amber is pushed
// only around the widget that carries a selection, and only while it does.
//
//   ScopedSelectionStyle selection(isSelected);
//   ImGui::TreeNodeEx(label, flags | (isSelected ? ImGuiTreeNodeFlags_Selected : 0));
//
// Draw-list outlines around the thing being edited use EditorUi::Selected and
// EditorUi::SelectedOutline directly.
class ScopedSelectionStyle
{
public:
    explicit ScopedSelectionStyle(bool selected);
    ~ScopedSelectionStyle();

    ScopedSelectionStyle(const ScopedSelectionStyle&) = delete;
    ScopedSelectionStyle& operator=(const ScopedSelectionStyle&) = delete;

private:
    bool Pushed;
};
