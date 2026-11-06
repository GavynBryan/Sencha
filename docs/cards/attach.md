# attach

Snapshot `97ed48e4` (2026-10-30). Layer: gameplay simulation.

## 1. Frame phases

- `ExtractRender`: `PlaceAttachments` (`engine/src/app/EngineFramePhases.cpp:1135`) composes attachments onto the drawn pose before transforms propagate.

## 2. Inputs

- The body's posed joints and transform history, read at the presentation alpha by `PlaceAttachments` (`engine/include/attach/Attachment.h:81`).
- Requests to attach, through `AttachToSocket` (`engine/include/attach/Attachment.h:40`) and the presentation-attach lease, `AttachmentOperations` (`engine/include/attach/AttachmentVerbs.h:22`).
- Module dependencies: anim, authored, core, ecs, math, net, presentation, render, world.

## 3. Outputs

- `Attachment` (`engine/include/attach/AttachmentComponents.h:38`) and `AttachmentSet` (`engine/include/attach/AttachmentComponents.h:33`): the attached entity and its holder's list; `Attachment` is read by render, graphics and physics.
- Placed transforms on attached entities: consumed by `PropagateTransforms` in the same pass.

## 4. Central decisions

- `PlaceAttachments` (`engine/src/attach/Attachment.cpp:462`): places every attachment at the joint or origin its body is drawn at this frame.
- `AttachmentOperations::Acquire` (`engine/src/attach/AttachmentVerbs.cpp:87`): grants, changes or refuses a held prop.
- `Link` (`engine/src/attach/Attachment.cpp:205`): inserts an attachment into its holder's list.

## 5. Invariants

- An attachment follows the joint at the pose its body is drawn at: `Attachments.AnAttachmentFollowsTheJointAtThePoseItsBodyIsDrawnAt` (`test/runtime/AttachmentTests.cpp`).
- An unknown socket is refused and nothing is made: `Attachments.AnUnknownSocketIsRefusedAndNothingIsMade` (`test/runtime/AttachmentTests.cpp`).
- What a body wears ends with it the same frame: `Attachments.WhatABodyWearsEndsWithItTheSameFrame` (`test/runtime/AttachmentTests.cpp`).
- What a body wears follows it to another partition: `Attachments.WhatABodyWearsFollowsItToAnotherPartition` (`test/runtime/AttachmentTests.cpp`).
- A steady frame allocates nothing: `Attachments.ASteadyFrameAllocatesNothing` (`test/runtime/AttachmentTests.cpp`).
- A holder that dies takes its prop within a frame: `PresentationAttachLease.AHolderThatDiesTakesItsPropWithinAFrame` (`test/runtime/AttachmentVerbTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 935 lines in 6 files.
- CCN 30 `PlaceAttachments` (`engine/src/attach/Attachment.cpp:462`); CCN 19 `AttachmentOperations::Acquire` (`engine/src/attach/AttachmentVerbs.cpp:87`); CCN 17 `Link` (`engine/src/attach/Attachment.cpp:205`).

## 7. Docs that contradict the code

- None found.
