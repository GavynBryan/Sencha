#include <anim/AnimTypes.h>

std::string_view AnimFactKindName(AnimFactKind kind)
{
    switch (kind)
    {
    case AnimFactKind::Bool: return "bool";
    case AnimFactKind::Float: return "float";
    case AnimFactKind::Int: return "int";
    case AnimFactKind::Tag: return "tag";
    case AnimFactKind::TagSet: return "tagset";
    }
    return "unknown";
}

std::string_view AnimCancelReasonName(AnimCancelReason reason)
{
    switch (reason)
    {
    case AnimCancelReason::None: return "none";
    case AnimCancelReason::Released: return "released";
    case AnimCancelReason::Interrupted: return "interrupted";
    case AnimCancelReason::Failed: return "failed";
    case AnimCancelReason::Superseded: return "superseded";
    }
    return "unknown";
}

std::string_view AnimRequestLifetimeName(AnimRequestLifetime lifetime)
{
    switch (lifetime)
    {
    case AnimRequestLifetime::Held: return "held";
    case AnimRequestLifetime::Fixed: return "fixed";
    case AnimRequestLifetime::Impulse: return "impulse";
    }
    return "unknown";
}
