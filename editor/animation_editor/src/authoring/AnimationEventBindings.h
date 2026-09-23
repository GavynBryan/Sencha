#pragma once

#include <anim/AnimRigBinding.h>
#include <anim/AnimationClip.h>
#include <authored/VerbBindingCompiler.h>
#include <core/json/JsonValue.h>

#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// Event bindings, as an author picks and fills them
//
// What the event inspector reads: the rig's bindings with the verb each
// invokes and the arguments each input fills, and an event's inputs checked
// against its binding by the same conversion the rig binding uses, so a value
// the inspector accepts is one the runtime accepts. Nothing here holds a
// compiled binding or a verb id past the call that read it.
//=============================================================================

struct AnimationBindingInputView
{
    std::string Name;
    // Each argument the input fills, by the verb's argument name, and its
    // kind as the verb declares it.
    std::vector<std::pair<std::string, DataFieldKind>> Destinations;
};

struct AnimationBindingView
{
    std::string Key;
    std::string Verb;
    std::vector<AnimationBindingInputView> Inputs;
};

// Every binding the rig's binding set compiled, in its order.
[[nodiscard]] std::vector<AnimationBindingView> DescribeAnimationBindings(const AnimBoundRig& rig,
                                                                         const VerbRegistry& verbs);

// One binding input, against what the event supplies for it.
struct AnimationEventInputCheck
{
    std::string Input;
    bool Supplied = false;
    bool Valid = false;
    // Why not, when it is not.
    std::string Message;
};

// One entry per input the binding takes, then one per value the event
// supplies that the binding does not take.
[[nodiscard]] std::vector<AnimationEventInputCheck> CheckAnimationEventInputs(
    const AnimationClipEvent& event, const CompiledVerbBinding& binding, const VerbBindingEnvironment& environment);

// A new binding record for `verb` whose every argument is an input of the
// same name: what "create binding" starts from, for the author to narrow.
[[nodiscard]] JsonValue MakeAnimationBindingRecord(std::string_view key, const VerbDefinition& verb);

// Appends `record` to an authored.bindings document root. False when its key
// is already declared there.
[[nodiscard]] bool AddAnimationBindingRecord(JsonValue& root, JsonValue record);
