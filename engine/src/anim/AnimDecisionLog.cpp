#include <anim/AnimDecisionLog.h>

std::string_view AnimDecisionCauseName(AnimDecisionCause cause)
{
    switch (cause)
    {
    case AnimDecisionCause::RequestAdded: return "RequestAdded";
    case AnimDecisionCause::RequestSuperseded: return "RequestSuperseded";
    case AnimDecisionCause::RequestDeduplicated: return "RequestDeduplicated";
    case AnimDecisionCause::RequestCancelled: return "RequestCancelled";
    case AnimDecisionCause::RequestExpired: return "RequestExpired";
    case AnimDecisionCause::RequestRejected: return "RequestRejected";
    }
    return "Unknown";
}

std::string_view AnimRejectReasonName(AnimRejectReason reason)
{
    switch (reason)
    {
    case AnimRejectReason::None: return "None";
    case AnimRejectReason::Capacity: return "Capacity";
    case AnimRejectReason::UndeclaredIntent: return "UndeclaredIntent";
    case AnimRejectReason::Malformed: return "Malformed";
    }
    return "Unknown";
}
