#pragma once

#include <anim/AnimRigBinding.h>
#include <anim/AnimationClip.h>
#include <authored/VerbBindingCompiler.h>
#include <core/json/JsonValue.h>

#include <string>
#include <string_view>
#include <vector>

struct AnimationBindingInputView
{
    std::string Name;
    // Verb argument name and declared kind of each argument the input fills.
    std::vector<std::pair<std::string, DataFieldKind>> Destinations;
};

struct AnimationBindingView
{
    std::string Key;
    std::string Verb;
    std::vector<AnimationBindingInputView> Inputs;
};

[[nodiscard]] std::vector<AnimationBindingView> DescribeAnimationBindings(const AnimBoundRig& rig,
                                                                         const VerbRegistry& verbs);

struct AnimationEventInputCheck
{
    std::string Input;
    bool Supplied = false;
    bool Valid = false;
    std::string Message;
};

// Uses the rig binding's own conversion, so an accepted value is one the runtime
// accepts. One entry per binding input, then one per unexpected event value.
[[nodiscard]] std::vector<AnimationEventInputCheck> CheckAnimationEventInputs(
    const AnimationClipEvent& event, const CompiledVerbBinding& binding, const VerbBindingEnvironment& environment);

// Every verb argument becomes an input of the same name.
[[nodiscard]] JsonValue MakeAnimationBindingRecord(std::string_view key, const VerbDefinition& verb);

// False when the key is already declared.
[[nodiscard]] bool AddAnimationBindingRecord(JsonValue& root, JsonValue record);
