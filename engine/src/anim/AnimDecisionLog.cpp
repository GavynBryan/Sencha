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
    case AnimDecisionCause::WinnerChanged: return "WinnerChanged";
    case AnimDecisionCause::LatchArmed: return "LatchArmed";
    case AnimDecisionCause::LatchReleased: return "LatchReleased";
    case AnimDecisionCause::LatchInterrupted: return "LatchInterrupted";
    case AnimDecisionCause::ContentChanged: return "ContentChanged";
    case AnimDecisionCause::Anchored: return "Anchored";
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

std::string_view AnimChangeReasonName(AnimChangeReason reason)
{
    switch (reason)
    {
    case AnimChangeReason::None: return "None";
    case AnimChangeReason::FactsChanged: return "FactsChanged";
    case AnimChangeReason::RequestsChanged: return "RequestsChanged";
    case AnimChangeReason::HoldExpired: return "HoldExpired";
    case AnimChangeReason::LatchComplete: return "LatchComplete";
    case AnimChangeReason::LatchInterrupted: return "LatchInterrupted";
    case AnimChangeReason::Rebound: return "Rebound";
    case AnimChangeReason::BehaviorChanged: return "BehaviorChanged";
    case AnimChangeReason::RowChanged: return "RowChanged";
    }
    return "Unknown";
}
