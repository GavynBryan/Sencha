#pragma once

#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <ecs/EntityId.h>
#include <ecs/LeaseOwner.h>
#include <presentation/PresentationOwner.h>

#include <string_view>
#include <vector>

class World;

// Holds a static mesh at a socket of the target while the lease is held: a
// prop an ability phase shows. Cosmetic, so it runs wherever its phase is presented.
inline constexpr std::string_view kPresentationAttachLease = "presentation.attach";

void DeclareAttachmentVerbs(VerbRegistrationScope& scope);

// The provider. An attachment whose holder dies ends with it, by
// PlaceAttachments, so nothing waits for another acquire or release.
class AttachmentOperations
{
public:
    explicit AttachmentOperations(World& world);

    AttachmentOperations(const AttachmentOperations&) = delete;
    AttachmentOperations& operator=(const AttachmentOperations&) = delete;

    VerbAdmission Attach(const VerbInvocation& invocation);
    [[nodiscard]] PresentationWriterId Writer() const { return WriterId; }

private:
    struct Held
    {
        LeaseOwner Owner;
        EntityId Attachment;
    };

    VerbAdmission Acquire(const VerbInvocation& invocation);
    void Release(const LeaseOwner& owner);

    World* Entities;
    PresentationWriterId WriterId;
    std::vector<Held> Holds;
};

inline VerbAdmission InvokePresentationAttach(AttachmentOperations& operations, const VerbInvocation& invocation)
{
    return operations.Attach(invocation);
}
