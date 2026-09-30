#pragma once

#include "commands/CommandStack.h"
#include "commands/ICommand.h"
#include "document/WorldDocument.h"

#include <memory>
#include <utility>

// One undo step for a manifest edit: add or rename a graph or zone, move a zone
// between graphs, retune streaming. The step carries whole before/after
// manifests rather than a per-verb delta, because the manifest is headers only
// (no scene content), so copying it is cheap, and because every one of those
// verbs is a small struct mutation that would otherwise need its own near
// identical command.
//
// Both directions install through WorldDocument::ApplyManifestSnapshot, so undo
// reconciles focus, selection, and open zones exactly as the original edit did.
//
// Manifest history rides the same stack as content edits and is dropped with it
// when a zone unload clears the stack; the manifest itself is unaffected.
class EditWorldManifestCommand final : public ICommand
{
public:
    EditWorldManifestCommand(WorldDocument& world, WorldPartitionManifest before,
                             WorldPartitionManifest after)
        : World(world), Before(std::move(before)), After(std::move(after)) {}

    void Execute() override { World.ApplyManifestSnapshot(After); }
    void Undo() override { World.ApplyManifestSnapshot(Before); }

private:
    WorldDocument& World;
    WorldPartitionManifest Before;
    WorldPartitionManifest After;
};

// Records a manifest edit that already happened as one undo step: capture the
// manifest before calling the verb, then hand that copy here once the verb
// reports success. A refused verb records nothing, so undo never grows a step
// that changes nothing.
//
// Re-executing installs the same manifest the verb produced, which is what redo
// needs; installing a manifest that is already current is a no-op beyond a
// validation pass, so the push costs nothing visible.
inline void RecordManifestEdit(WorldDocument& world, CommandStack& commands,
                               WorldPartitionManifest before)
{
    commands.Execute(std::make_unique<EditWorldManifestCommand>(world, std::move(before),
                                                                world.Manifest()));
}
