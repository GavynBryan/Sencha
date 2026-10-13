#pragma once

#include "viewport/ViewportArrangement.h"

#include <math/Vec.h>

class ActionCatalog;
class ActionContext;
class ConsoleRegistry;
class ViewportLayout;

// How the views share the content and where they meet, kept in archived cvars
// (editor.view.arrangement, editor.view.divider_x and _y) so the choice
// outlives the session; and the actions that choose it, and which view a
// single view shows.
class ViewArrangementSettings
{
public:
    explicit ViewArrangementSettings(ConsoleRegistry& console);

    [[nodiscard]] ViewportArrangementKind Kind() const;
    void SetKind(ViewportArrangementKind kind);
    // Where the views meet, as fractions of the content's width and height.
    [[nodiscard]] Vec2d Divider() const;
    void SetDivider(Vec2d divider);

    void Offer(ActionCatalog& catalog, ActionContext& context, ViewportLayout& layout);

private:
    ConsoleRegistry& Console;
};
