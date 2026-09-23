// What the animation viewport composes from a simulated rig: every layer's
// playing clip at its time and weight over its mask, less the layers the
// author muted or soloed away -- a display choice that never reaches the rig.

#include "authoring/AnimationPreviewWorkspace.h"

#include <anim/AnimationClipCache.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
    constexpr std::string_view kSkeleton = "asset://meshes/biped.sskel";

    struct DisplayFixture
    {
        AnimationClipCache Clips;
        AnimBoundRig Rig;
        AnimContentState Content;

        DisplayFixture()
        {
            const auto clip = [&](std::string path, std::string_view skeleton) {
                AnimationClipData data;
                data.DurationSeconds = 1.0f;
                data.SkeletonPath = std::string(skeleton);
                AnimBoundContent content;
                content.Path = path;
                content.Clip = Clips.Register(path, std::move(data), {});
                Rig.Contents.push_back(std::move(content));
            };
            clip("asset://anim/walk.sanim", kSkeleton);
            clip("asset://anim/aim.sanim", kSkeleton);
            clip("asset://anim/other.sanim", "asset://meshes/quadruped.sskel");

            for (const char* name : { "anim.layer.base", "anim.layer.upper", "anim.layer.face" })
            {
                AnimBoundLayer layer;
                layer.NameText = name;
                Rig.Layers.push_back(std::move(layer));
            }
            Rig.Layers[1].Mode = AnimLayerMode::Additive;
            Rig.Layers[1].Weight = 0.5f;
            Rig.Layers[1].Mask = { 0, 1, 1 };
            Content.Layers[0].Clip = 0;
            Content.Layers[0].TimeSeconds = 0.25f;
            Content.Layers[1].Clip = 1;
            Content.Layers[1].TimeSeconds = 0.5f;
            Content.Layers[2].Clip = 2;
        }

        std::vector<AnimPoseLayer> Layers(const AnimationLayerDisplay& display, std::string& note)
        {
            return AnimationPreviewPoseLayers(Rig, Content, nullptr, Clips, display, kSkeleton, note);
        }
    };
}

TEST(AnimationLayerDisplay, EveryShownLayerComposesAtItsTimeWeightAndMask)
{
    DisplayFixture fx;
    std::string note;
    const std::vector<AnimPoseLayer> layers = fx.Layers({}, note);
    ASSERT_EQ(layers.size(), 2u) << note;
    EXPECT_EQ(layers[0].Clip, fx.Clips.Get(fx.Rig.Contents[0].Clip));
    EXPECT_FLOAT_EQ(layers[0].TimeSeconds, 0.25f);
    EXPECT_TRUE(layers[0].Mask.empty());
    EXPECT_EQ(layers[1].Mode, AnimLayerMode::Additive);
    EXPECT_FLOAT_EQ(layers[1].Weight, 0.5f);
    EXPECT_EQ(layers[1].Mask.size(), 3u);
    // The face layer's clip animates another skeleton, and the note says so.
    EXPECT_NE(note.find("anim.layer.face: asset://anim/other.sanim animates another skeleton"), std::string::npos)
        << note;
}

TEST(AnimationLayerDisplay, MuteAndSoloChooseWhatShowsWithoutTouchingTheRig)
{
    DisplayFixture fx;
    std::string note;

    AnimationLayerDisplay muted;
    muted.Muted = 0b001;
    std::vector<AnimPoseLayer> layers = fx.Layers(muted, note);
    ASSERT_EQ(layers.size(), 1u);
    EXPECT_EQ(layers[0].Mode, AnimLayerMode::Additive);

    AnimationLayerDisplay soloed;
    soloed.Soloed = 0b001;
    layers = fx.Layers(soloed, note);
    ASSERT_EQ(layers.size(), 1u);
    EXPECT_EQ(layers[0].Mode, AnimLayerMode::Override);

    // Muting wins over a solo on the same layer.
    soloed.Muted = 0b001;
    EXPECT_TRUE(fx.Layers(soloed, note).empty());

    EXPECT_FLOAT_EQ(fx.Rig.Layers[1].Weight, 0.5f);
    EXPECT_EQ(fx.Content.Layers[1].Clip, 1u);
}
