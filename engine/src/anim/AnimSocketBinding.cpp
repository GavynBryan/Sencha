#include "AnimRigBinder.h"

#include <format>

// A socket that cannot bind is left out, never bound to a guess: what attaches to
// it is refused by name instead of drawn at the wrong joint. The rig still plays.
void AnimRigBinder::BindSockets(const AnimRigData& rig, const SkeletonData* skeleton,
                                const SkeletonJointNames& jointNames)
{
    for (std::size_t i = 0; i < rig.Sockets.size(); ++i)
    {
        const AnimRigSocket& socket = rig.Sockets[i];
        const std::string field = std::format("$.data.sockets[{}].joint", i);
        if (rig.SkeletonPath.empty())
        {
            Warning("anim.socket.no_skeleton", Out.RigPath, field,
                    std::format("'{}' names a joint, so the rig names the skeleton it belongs to.", socket.Name));
            continue;
        }
        if (skeleton == nullptr)
            continue;

        const SkeletonJointNames::Match found = jointNames.Find(socket.Joint);
        if (found.Result == SkeletonJointNames::Status::Unknown)
        {
            Warning("anim.socket.joint_unknown", Out.RigPath, field,
                    std::format("'{}' has no joint named '{}', so nothing can attach to '{}'.", rig.SkeletonPath,
                                socket.Joint, socket.Name));
            continue;
        }
        if (found.Result == SkeletonJointNames::Status::Ambiguous)
        {
            Warning("anim.socket.joint_ambiguous", Out.RigPath, field,
                    std::format("'{}' has more than one joint named '{}', so nothing can attach to '{}'; "
                                "reimport it so every joint has its own name.",
                                rig.SkeletonPath, socket.Joint, socket.Name));
            continue;
        }
        Out.SocketsByName.emplace(socket.Name,
                                  RigSocketIndex{ static_cast<std::uint16_t>(Out.Sockets.size()) });
        Out.Sockets.push_back(AnimBoundSocket{ socket.Name, found.Joint, socket.Offset });
    }
}
