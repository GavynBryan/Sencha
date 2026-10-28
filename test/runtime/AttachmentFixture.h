#pragma once

#include "AnimRigFixture.h"
#include "CountedPathStore.h"

#include <anim/AnimPosePool.h>
#include <anim/AnimPoseState.h>
#include <anim/AnimRig.h>
#include <attach/Attachment.h>
#include <core/assets/AssetStoreTable.h>
#include <math/MathSchemas.h>
#include <render/MaterialCache.h>
#include <render/MaterialSetCache.h>
#include <render/PresentedWith.h>
#include <render/StaticMeshComponent.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

#include <format>
#include <string>
#include <vector>

// A body with an arm rig (root > spine > hand, a socket hand_r on the hand) in
// a World where attachments are installed, with a mesh and a material resident
// by path.
namespace attach_test
{
    constexpr const char* kSkeleton = "asset://anim/a.sskel";
    constexpr const char* kRig = "asset://anim/a.rig.sdata";
    constexpr StoragePartitionId kZone{ 3 };
    constexpr StoragePartitionId kElsewhere{ 4 };

    // root > spine > hand, each a unit up its parent at bind.
    inline SkeletonData Arm()
    {
        SkeletonData skeleton;
        for (int j = 0; j < 3; ++j)
        {
            SkeletonJoint joint;
            joint.Name = j == 0 ? "root" : j == 1 ? "spine" : "hand";
            joint.ParentIndex = j - 1;
            joint.BindTranslation = j == 0 ? Vec3d::Zero() : Vec3d(0.0f, 1.0f, 0.0f);
            skeleton.Joints.push_back(joint);
        }
        return skeleton;
    }

    struct AttachFixture : AnimRigFixture
    {
        CountedPathStore Meshes{ AssetType::StaticMesh };
        MaterialCache Materials;
        MaterialSetCache Sets{ &Materials };
        std::uint64_t PropMesh = 0;
        MaterialHandle PropMaterial;
        SkeletonHandle Skeleton;
        DataAssetHandle RigHandle;
        EntityId Body;

        explicit AttachFixture(bool install = true)
        {
            PropMesh = Meshes.Add("asset://meshes/prop.smesh");
            PropMaterial = Materials.Register("asset://materials/prop.smat", Material{});
            AssetStoreTable stores;
            stores.Add(AssetType::StaticMesh, AssetArity::Single, Meshes);
            stores.Add(AssetType::Material, AssetArity::Single, Materials);
            stores.Add(AssetType::Material, AssetArity::List, Sets);
            Entities.SetResource(stores);
            ComponentStorageTraits<LocalTransform>::Register(Entities);
            Entities.RegisterComponent<WorldTransformHistory>();
            Entities.RegisterComponent<StaticMeshComponent>();
            Entities.RegisterComponent<PresentedWith>();
            Entities.RegisterComponent<AttachmentSet>();
            Entities.RegisterComponent<Attachment>();
            if (install)
                InstallAttachments(Entities, nullptr);

            Skeleton = Skeletons.Register(kSkeleton, Arm());
            (void)Tags().RegisterTag("Anim.Idle");
            Clip("asset://anim/idle.sanim", 1.0f, {}, kSkeleton);
            (void)Load("asset://anim/a.behaviors.sdata", kAnimBehaviorSetType,
                       R"({ "behaviors": [ { "tag": "Anim.Idle", "kind": "cyclic" } ] })");
            (void)Load("asset://anim/a.slots.sdata", kAnimSlotMapType,
                       R"({ "rows": [ { "id": "idle", "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" } ] })");
            RigHandle = Load(kRig, kAnimRigType, RigJson(R"([ { "name": "hand_r", "joint": "hand", "position": [0, 0, 0.5] } ])"));
            Body = MakeBody(kZone);
        }

        // The stores die before the base's World, so its teardown must not reach them.
        ~AttachFixture() { Entities.SetResource(AssetStoreTable{}); }

        static std::string RigJson(std::string_view sockets)
        {
            return std::format(R"({{ "skeleton": "{}",
                "behaviors": [ "asset://anim/a.behaviors.sdata" ], "slot_maps": [ "asset://anim/a.slots.sdata" ],
                "layers": [ {{ "name": "anim.layer.base", "idle": "Anim.Idle" }} ], "sockets": {} }})",
                               kSkeleton, sockets);
        }

        EntityId MakeBody(StoragePartitionId partition)
        {
            const EntityId body = Entities.CreateEntity(partition);
            (void)ComponentStorageTraits<LocalTransform>::Add(Entities, body, LocalTransform{});
            Entities.AddComponent<AnimRig>(body, AnimRig{ RigHandle });
            return body;
        }

        // A pose whose hand joint sits `reach` along X instead of up its parent.
        void Pose(EntityId body, Vec3d previousReach, Vec3d reach)
        {
            AnimPosePool& pool = Entities.HasResource<AnimPosePool>() ? Entities.GetResource<AnimPosePool>()
                                                                      : Entities.AddResource<AnimPosePool>();
            AnimPoseState state;
            state.Slot = pool.Allocate(body);
            state.BindingGeneration = Bound(RigHandle).Generation;
            AnimPosePool::Slot& slot = *pool.Find(state.Slot, body);
            slot.Shape(3, 1);
            slot.Skeleton = Skeleton;
            for (std::size_t j = 0; j < 3; ++j)
            {
                slot.Previous[j].Position = j == 0 ? Vec3d::Zero() : Vec3d(0.0f, 1.0f, 0.0f);
                slot.Current[j].Position = slot.Previous[j].Position;
            }
            slot.Previous[2].Position = previousReach;
            slot.Current[2].Position = reach;
            slot.HasCurrent = true;
            slot.HasPrevious = true;
            ++slot.Revision;
            if (AnimPoseState* existing = Entities.TryGet<AnimPoseState>(body))
                *existing = state;
            else
                Entities.AddComponent<AnimPoseState>(body, state);
        }

        [[nodiscard]] const Transform3f& Local(EntityId entity) const
        {
            const World& reader = Entities;
            return reader.TryGet<LocalTransform>(entity)->Value;
        }

        [[nodiscard]] std::vector<EntityId> Worn(EntityId root) const
        {
            std::vector<EntityId> worn;
            ForEachAttachment(Entities, root, [&](EntityId entity, const Attachment&) { worn.push_back(entity); });
            return worn;
        }

        [[nodiscard]] EntityId Mesh(std::string_view socket, PresentationOwner owner = {}, EntityId mount = {})
        {
            AttachResult result = AttachResult::NoRoot;
            const EntityId entity = CreateMeshAttachment(
                Entities, { .Root = Body, .Socket = socket, .Mount = mount, .Placement = { .Offset = {}, .Owner = owner },
                            .Mesh = {}, .Materials = {}, .Visible = true, .CastShadows = true },
                &result);
            EXPECT_EQ(result, AttachResult::Attached);
            return entity;
        }
    };
}
