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
    case AnimDecisionCause::RequestOrphaned: return "RequestOrphaned";
    case AnimDecisionCause::RequestRejected: return "RequestRejected";
    case AnimDecisionCause::WinnerChanged: return "WinnerChanged";
    case AnimDecisionCause::LatchArmed: return "LatchArmed";
    case AnimDecisionCause::LatchReleased: return "LatchReleased";
    case AnimDecisionCause::LatchInterrupted: return "LatchInterrupted";
    case AnimDecisionCause::ContentChanged: return "ContentChanged";
    case AnimDecisionCause::IndexReset: return "IndexReset";
    case AnimDecisionCause::SectionChanged: return "SectionChanged";
    case AnimDecisionCause::EventCrossed: return "EventCrossed";
    case AnimDecisionCause::BehaviorEntered: return "BehaviorEntered";
    case AnimDecisionCause::BehaviorExited: return "BehaviorExited";
    case AnimDecisionCause::SectionEntered: return "SectionEntered";
    case AnimDecisionCause::SectionExited: return "SectionExited";
    case AnimDecisionCause::WeightChanged: return "WeightChanged";
    case AnimDecisionCause::BlendApplied: return "BlendApplied";
    case AnimDecisionCause::TimingDisagreed: return "TimingDisagreed";
    case AnimDecisionCause::TimingAgreed: return "TimingAgreed";
    }
    return "Unknown";
}

std::string_view AnimEventOutcomeName(AnimEventOutcome outcome)
{
    switch (outcome)
    {
    case AnimEventOutcome::Fired: return "Fired";
    case AnimEventOutcome::Skipped: return "Skipped";
    case AnimEventOutcome::BelowWeight: return "BelowWeight";
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
    case AnimRejectReason::LeftToAuthority: return "LeftToAuthority";
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
    case AnimChangeReason::FlowStarted: return "FlowStarted";
    case AnimChangeReason::SectionFollowed: return "SectionFollowed";
    case AnimChangeReason::SectionLooped: return "SectionLooped";
    case AnimChangeReason::SectionBranched: return "SectionBranched";
    case AnimChangeReason::SectionCancelled: return "SectionCancelled";
    case AnimChangeReason::FlowAnchored: return "FlowAnchored";
    case AnimChangeReason::RequestSuperseded: return "RequestSuperseded";
    case AnimChangeReason::RequestCorrected: return "RequestCorrected";
    case AnimChangeReason::Reconstructed: return "Reconstructed";
    }
    return "Unknown";
}
