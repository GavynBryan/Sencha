# presentation

Snapshot `97ed48e4` (2026-10-30). Layer: presentation.

## 1. Frame phases

- None of its own: it is a registry of presentation writers that render's mesh overrides and attachments consult when they run.

## 2. Inputs

- Writer registrations, `RegisterPresentationWriter` (`engine/include/presentation/PresentationOwner.h:47`), one per module that writes presentation state.
- Module dependencies: ecs.

## 3. Outputs

- `PresentationOwner` (`engine/include/presentation/PresentationOwner.h:25`): which writer owns a piece of presentation state, consumed by render (`engine/src/render/MeshOverrides.cpp`) and attach (`engine/src/attach/Attachment.cpp`).
- `PresentationWriters` (`engine/include/presentation/PresentationOwner.h:37`): writer names for diagnostics.

## 4. Central decisions

- `PresentationOwnerPrecedes` (`engine/include/presentation/PresentationOwner.h:34`): decides which of two writers wins when both write the same state.
- `RegisterPresentationWriter` (`engine/src/presentation/PresentationOwner.cpp:25`): mints a writer id for a World.

## 5. Invariants

- None with a direct test.

### Unchecked

- Writer precedence: no test calls `PresentationOwnerPrecedes` or `RegisterPresentationWriter` directly; it is exercised only through `Attachments.AWritersAttachmentsGoWithIt` (`test/runtime/AttachmentTests.cpp`).

## 6. Size

- 77 lines in 2 files.
- CCN 3 `PresentationWriters::NameOf` (`engine/src/presentation/PresentationOwner.cpp:19`); CCN 2 `RegisterPresentationWriter` (`engine/src/presentation/PresentationOwner.cpp:25`); CCN 1 `PresentationWriterId::IsValid` (`engine/include/presentation/PresentationOwner.h:18`).

## 7. Docs that contradict the code

- None found.
