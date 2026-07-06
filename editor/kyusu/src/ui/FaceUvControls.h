#pragma once

#include "meshedit/FaceMaterialEdits.h"

#include <optional>

class CommandStack;
class SelectionService;
struct IMeshEditTarget;

// The face UV projection rows of the Mesh Edit panel's Face context: axis
// alignment, scale/shift/rotate, justify, and the projection clipboard. Owns
// only transient widget state (the drag buffer, treat-as-one); every commit
// routes through FaceMaterialEdits as one undoable command per brush.
class FaceUvControls
{
public:
    void Draw(IMeshEditTarget& target,
              const SelectionService& selection,
              CommandStack& commands,
              std::optional<FaceMaterialClipboard>& clipboard);

private:
    // In-progress UV drag. A drag mutates this buffer (so it accumulates instead
    // of being reset to the scene value each frame) and commits one command when
    // it ends; the buffer is reseeded from the selection only while not editing.
    UvProjection UvEdit;
    bool         UvEditing = false;

    // Justify treats the whole selection as one unit when set (one continuous
    // mapping across faces and brushes); per-face justify otherwise.
    bool TreatAsOne = false;
};
