#include "BrushModifierEditorAdapter.h"

#include "ui/chrome/ChromeControls.h"

#include "brush/BrushEvaluation.h"
#include "brush/BrushModifier.h"
#include "commands/CommandStack.h"
#include "document/BrushComponents.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/commands/ValueCommand.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

namespace
{
    // Display names, indexed like BrushModifierKinds(): the one place a kind
    // is named for a person.
    constexpr std::array<const char*, 2> kKindLabels = { "Mirror", "Array" };
    static_assert(kKindLabels.size() == std::variant_size_v<decltype(BrushModifier::Params)>);

    const char* AxisLabel(LocalAxis axis)
    {
        return axis == LocalAxis::Y ? "Y" : axis == LocalAxis::Z ? "Z" : "X";
    }

    // The header's one-line reading of a row, so a collapsed stack still says
    // what it does: "X · Origin", "X · ×5 · gap 16".
    struct Summary
    {
        std::string operator()(const MirrorModifier& mirror) const
        {
            std::string text = AxisLabel(mirror.Axis);
            switch (mirror.Source)
            {
            case MirrorPlaneSource::BoundsCenter: text += " \xC2\xB7 Bounds Center"; break;
            case MirrorPlaneSource::Custom:       text += " \xC2\xB7 Custom plane"; break;
            case MirrorPlaneSource::Origin:
            default:                              text += " \xC2\xB7 Origin"; break;
            }
            if (mirror.Source != MirrorPlaneSource::Custom && std::abs(mirror.Offset) > 1e-6f)
            {
                char buf[32];
                std::snprintf(buf, sizeof(buf), " %+.3g", mirror.Offset);
                text += buf;
            }
            return text;
        }

        std::string operator()(const ArrayModifier& array) const
        {
            char buf[96];
            if (array.Placement == ArrayPlacement::ConstantOffset)
                std::snprintf(buf, sizeof(buf), "%s%s \xC2\xB7 \xC3\x97%d \xC2\xB7 offset (%.3g, %.3g, %.3g)",
                              array.Reverse ? "-" : "", AxisLabel(array.Axis), array.Count,
                              array.Offset.X, array.Offset.Y, array.Offset.Z);
            else
                std::snprintf(buf, sizeof(buf), "%s%s \xC2\xB7 \xC3\x97%d \xC2\xB7 gap %.3g",
                              array.Reverse ? "-" : "", AxisLabel(array.Axis), array.Count,
                              array.Spacing);
            return buf;
        }
    };

    // A numeric drag edits the live stack every frame (immediate feedback) and
    // commits one command when the drag ends; an interrupted drag (Escape,
    // focus loss) restores what the drag started from. One drag at a time can
    // be live, so the pre-drag stack lives here.
    struct DragTransaction
    {
        EntityId Entity;
        BrushModifierStack Before;
    };

    class BrushModifierEditorAdapter final : public IEditorComponentAdapter
    {
    public:
        [[nodiscard]] ComponentTypeId Type() const override
        {
            return ResolveComponentTypeId<BrushComponent>();
        }

        bool DrawInspector(EditorComponentInspectorContext& context) const override
        {
            const BrushComponent* brush = context.Scene.TryGetBrush(context.Entity);
            const BrushModifierStack* stored = context.Scene.TryGetBrushModifiers(context.Entity);
            if (brush == nullptr || stored == nullptr)
                return false;

            ImGui::TextDisabled("Brush %u", static_cast<unsigned>(brush->Id.Value));

            // Work on a copy: a row's widgets may change the stack, and the
            // stored one is replaced by the command that lands the change.
            BrushModifierStack stack = *stored;
            const BrushEvaluated* evaluated = context.Scene.TryGetBrushPieces(context.Entity);
            std::optional<std::uint32_t> failedRow;
            bool failsHardLimit = false;
            if (evaluated != nullptr && evaluated->Status != BrushEvaluationStatus::Ok)
            {
                failedRow = evaluated->FailedModifier;
                // Projected from the stack's factors, not from a second evaluation
                // under the cook policy: that would re-evaluate every frame against
                // the render path's cached preview result.
                failsHardLimit = BrushProjectedPieceCount(stack) > BrushEvaluationPolicy::kHardPieceLimit;
            }

            ImGui::SeparatorText("Modifiers");
            if (stack.empty())
                ImGui::TextDisabled("None");

            std::optional<BrushModifierStack> committed;
            for (std::size_t i = 0; i < stack.size() && !committed.has_value(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                BrushModifier& modifier = stack[i];

                bool enabled = modifier.Enabled;
                if (ImGui::Checkbox("##enabled", &enabled))
                {
                    modifier.Enabled = enabled;
                    committed = stack;
                }
                ImGui::SameLine();
                const std::string summary = std::visit(Summary{}, modifier.Params);
                const bool open = ImGui::TreeNodeEx(
                    "##row", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap,
                    "%s", kKindLabels[modifier.Params.index()]);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", summary.c_str());

                // Reorder and remove, right-aligned on the header line.
                const float button = ImGui::GetTextLineHeight() + 4.0f;
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                ImGui::SameLine(ImGui::GetContentRegionMax().x - 3.0f * button - 2.0f * spacing);
                if (EditorChrome::Button("up", "^", ImVec2(button, button), EditorChrome::ButtonTone::Normal)
                    && i > 0)
                {
                    std::swap(stack[i], stack[i - 1]);
                    committed = stack;
                }
                ImGui::SameLine();
                if (EditorChrome::Button("down", "v", ImVec2(button, button), EditorChrome::ButtonTone::Normal)
                    && i + 1 < stack.size())
                {
                    std::swap(stack[i], stack[i + 1]);
                    committed = stack;
                }
                ImGui::SameLine();
                if (EditorChrome::IconButton("remove", IconId::Delete, button,
                                             EditorChrome::ButtonTone::Normal))
                {
                    stack.erase(stack.begin() + static_cast<std::ptrdiff_t>(i));
                    committed = stack;
                }

                if (failedRow == i)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                                       failsHardLimit
                                           ? "Exceeds the piece limit: not evaluated, and the level will not cook."
                                           : "Exceeds the preview piece budget: not shown (editor.brush.preview_piece_budget).");
                }

                if (open)
                {
                    if (!committed.has_value())
                        DrawParams(context, stack, i, committed);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            // One entry point for every kind, present and future.
            if (EditorChrome::Button("add_modifier", "+ Add Modifier", {}, EditorChrome::ButtonTone::Normal))
                ImGui::OpenPopup("##add_modifier");
            if (ImGui::BeginPopup("##add_modifier"))
            {
                const std::span<const BrushModifierKindInfo> kinds = BrushModifierKinds();
                for (std::size_t k = 0; k < kinds.size(); ++k)
                {
                    if (ImGui::Selectable(kKindLabels[k]))
                    {
                        stack.push_back(kinds[k].MakeDefault());
                        committed = stack;
                    }
                }
                ImGui::EndPopup();
            }

            if (committed.has_value())
                Commit(context, *stored, std::move(*committed));
            return true;
        }

    private:
        static void Commit(EditorComponentInspectorContext& context, BrushModifierStack before,
                           BrushModifierStack after)
        {
            context.Commands.Execute(MakeEditBrushModifiersCommand(
                context.Entity, std::move(before), std::move(after), context.Scene, context.Document));
        }

        // Routes a drag widget's lifecycle: preview writes on every frame,
        // one command on release, restore on interruption. `changed` is the
        // widget's return this frame.
        void FinishDrag(EditorComponentInspectorContext& context, const BrushModifierStack& stored,
                        const BrushModifierStack& edited, bool changed) const
        {
            if (ImGui::IsItemActivated())
                Drag = DragTransaction{ context.Entity, stored };
            if (changed)
                context.Scene.SetBrushModifiers(context.Entity, edited);
            if (!ImGui::IsItemDeactivated())
                return;
            if (ImGui::IsItemDeactivatedAfterEdit() && Drag.has_value() && Drag->Entity == context.Entity)
                Commit(context, Drag->Before, edited);
            else if (Drag.has_value() && Drag->Entity == context.Entity)
                context.Scene.SetBrushModifiers(context.Entity, Drag->Before);
            Drag.reset();
        }

        // X / Y / Z as one row of toggles. True when the axis changed.
        static bool DrawAxis(LocalAxis& axis)
        {
            ImGui::TextUnformatted("Axis");
            ImGui::SameLine();
            bool changed = false;
            for (const LocalAxis candidate : { LocalAxis::X, LocalAxis::Y, LocalAxis::Z })
            {
                if (candidate != LocalAxis::X)
                    ImGui::SameLine();
                const bool active = axis == candidate;
                if (EditorChrome::Button(AxisLabel(candidate), AxisLabel(candidate), {},
                                         active ? EditorChrome::ButtonTone::Active
                                                : EditorChrome::ButtonTone::Normal)
                    && !active)
                {
                    axis = candidate;
                    changed = true;
                }
            }
            return changed;
        }

        void DrawParams(EditorComponentInspectorContext& context, BrushModifierStack& stack,
                        std::size_t index, std::optional<BrushModifierStack>& committed) const
        {
            const BrushModifierStack* stored = context.Scene.TryGetBrushModifiers(context.Entity);
            if (stored == nullptr)
                return;
            BrushModifier& modifier = stack[index];

            if (auto* mirror = std::get_if<MirrorModifier>(&modifier.Params))
            {
                if (mirror->Source != MirrorPlaneSource::Custom)
                {
                    if (DrawAxis(mirror->Axis))
                    {
                        committed = stack;
                        return;
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(90.0f);
                    const bool changed = ImGui::DragFloat("Offset", &mirror->Offset, 0.05f);
                    FinishDrag(context, *stored, stack, changed);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Distance of the mirror plane from the brush origin along the axis.\n"
                                          "Move the origin (Set origin: To pivot / vertex / bounds) to move the mirror.");
                }
                if (ImGui::TreeNodeEx("Advanced", ImGuiTreeNodeFlags_None))
                {
                    static constexpr const char* kSources[] = { "Origin", "Bounds Center", "Custom plane" };
                    int source = static_cast<int>(mirror->Source);
                    ImGui::SetNextItemWidth(150.0f);
                    if (ImGui::Combo("Plane", &source, kSources, 3))
                    {
                        mirror->Source = static_cast<MirrorPlaneSource>(source);
                        committed = stack;
                        ImGui::TreePop();
                        return;
                    }
                    if (mirror->Source == MirrorPlaneSource::Custom)
                    {
                        float normal[3] = { mirror->CustomPlane.Normal.X, mirror->CustomPlane.Normal.Y,
                                            mirror->CustomPlane.Normal.Z };
                        bool changed = ImGui::DragFloat3("Normal", normal, 0.01f);
                        if (changed)
                            mirror->CustomPlane.Normal = { normal[0], normal[1], normal[2] };
                        FinishDrag(context, *stored, stack, changed);
                        float offset = -mirror->CustomPlane.D;
                        changed = ImGui::DragFloat("Distance", &offset, 0.05f);
                        if (changed)
                            mirror->CustomPlane.D = -offset;
                        FinishDrag(context, *stored, stack, changed);
                    }
                    ImGui::TreePop();
                }
            }
            else if (auto* array = std::get_if<ArrayModifier>(&modifier.Params))
            {
                if (DrawAxis(array->Axis))
                {
                    committed = stack;
                    return;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0f);
                int count = array->Count;
                if (ImGui::InputInt("Count", &count))
                {
                    array->Count = std::clamp(count, 1,
                                              static_cast<int>(BrushEvaluationPolicy::kHardPieceLimit));
                    committed = stack;
                    return;
                }
                if (array->Placement == ArrayPlacement::RelativeToBounds)
                {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80.0f);
                    const bool changed = ImGui::DragFloat("Gap", &array->Spacing, 0.05f);
                    FinishDrag(context, *stored, stack, changed);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Empty space between copies. 0 makes them touch;\n"
                                          "resizing the brush keeps them touching.");
                }
                if (ImGui::TreeNodeEx("Advanced", ImGuiTreeNodeFlags_None))
                {
                    bool reverse = array->Reverse;
                    if (ImGui::Checkbox("Reverse direction", &reverse))
                    {
                        array->Reverse = reverse;
                        committed = stack;
                        ImGui::TreePop();
                        return;
                    }
                    static constexpr const char* kPlacements[] = { "Relative to bounds", "Constant offset" };
                    int placement = static_cast<int>(array->Placement);
                    ImGui::SetNextItemWidth(150.0f);
                    if (ImGui::Combo("Placement", &placement, kPlacements, 2))
                    {
                        array->Placement = static_cast<ArrayPlacement>(placement);
                        committed = stack;
                        ImGui::TreePop();
                        return;
                    }
                    if (array->Placement == ArrayPlacement::ConstantOffset)
                    {
                        float offset[3] = { array->Offset.X, array->Offset.Y, array->Offset.Z };
                        const bool changed = ImGui::DragFloat3("Offset", offset, 0.05f);
                        if (changed)
                            array->Offset = { offset[0], offset[1], offset[2] };
                        FinishDrag(context, *stored, stack, changed);
                    }
                    ImGui::TreePop();
                }
            }
        }

        mutable std::optional<DragTransaction> Drag;
    };
}

std::unique_ptr<IEditorComponentAdapter> MakeBrushModifierEditorAdapter()
{
    return std::make_unique<BrushModifierEditorAdapter>();
}
