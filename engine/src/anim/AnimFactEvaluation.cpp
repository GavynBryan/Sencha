#include <anim/AnimFactEvaluation.h>

#include <algorithm>
#include <cmath>

float AnimFactToNumber(AnimFactKind kind, std::uint32_t bits)
{
    switch (kind)
    {
    case AnimFactKind::Float: return AnimFactToFloat(bits);
    case AnimFactKind::Int: return static_cast<float>(AnimFactToInt(bits));
    case AnimFactKind::Bool:
    case AnimFactKind::TagSet: return AnimFactToBool(bits) ? 1.0f : 0.0f;
    case AnimFactKind::Tag: return static_cast<float>(bits);
    }
    return 0.0f;
}

namespace
{
    bool ReadBool(std::span<const std::uint32_t> values, const AnimBoundOperand& operand)
    {
        return AnimFactToBool(values[operand.Slot]) != operand.Negate;
    }

    float ReadNumber(std::span<const std::uint32_t> values, const AnimBoundDerivation& derivation)
    {
        return AnimFactToNumber(derivation.SourceKind, values[derivation.Sources[0].Slot]);
    }

    double ElapsedMs(AnimTick since, AnimTick now, double tickSeconds)
    {
        return static_cast<double>(now - since) * tickSeconds * 1000.0;
    }

    bool Compare(AnimCompareOp op, float lhs, float rhs)
    {
        switch (op)
        {
        case AnimCompareOp::Lt: return lhs < rhs;
        case AnimCompareOp::Le: return lhs <= rhs;
        case AnimCompareOp::Gt: return lhs > rhs;
        case AnimCompareOp::Ge: return lhs >= rhs;
        case AnimCompareOp::Eq: return lhs == rhs;
        case AnimCompareOp::Ne: return lhs != rhs;
        }
        return false;
    }

    std::uint32_t Evaluate(const AnimBoundDerivation& derivation,
                           std::span<const std::uint32_t> values,
                           AnimDerivationMemory& memory,
                           AnimTick now,
                           double tickSeconds)
    {
        switch (derivation.Op)
        {
        case AnimDerivationOp::Edge:
        {
            const bool input = ReadBool(values, derivation.Sources[0]);
            const bool previous = AnimFactToBool(memory.LastInput);
            if (memory.Seen && input != previous && input == derivation.Rising)
                memory.MarkTick = now;
            memory.LastInput = AnimFactFromBool(input);
            const bool held = memory.MarkTick != kAnimNoTick
                && ElapsedMs(memory.MarkTick, now, tickSeconds) <= derivation.WindowMs;
            return AnimFactFromBool(held);
        }
        case AnimDerivationOp::TimeSince:
        {
            const bool input = ReadBool(values, derivation.Sources[0]);
            if (input == derivation.MatchValue)
                memory.MarkTick = now;
            const double capMs = derivation.WindowMs;
            const double elapsedMs = memory.MarkTick == kAnimNoTick
                ? capMs
                : std::min(ElapsedMs(memory.MarkTick, now, tickSeconds), capMs);
            return AnimFactFromFloat(static_cast<float>(elapsedMs / 1000.0));
        }
        case AnimDerivationOp::Hysteresis:
        {
            const float input = ReadNumber(values, derivation);
            // The schema guarantees Enter is above Exit. Unseen, the latch
            // takes whichever side of Enter the value is on.
            const bool latched = memory.Seen && memory.Value != 0.0f
                ? input > derivation.Exit
                : input >= derivation.Enter;
            memory.Value = latched ? 1.0f : 0.0f;
            return AnimFactFromBool(latched);
        }
        case AnimDerivationOp::MinDuration:
        {
            const bool input = ReadBool(values, derivation.Sources[0]);
            if (!input)
                memory.MarkTick = kAnimNoTick;
            else if (memory.MarkTick == kAnimNoTick)
                memory.MarkTick = now;
            const bool held = input
                && ElapsedMs(memory.MarkTick, now, tickSeconds) >= derivation.WindowMs;
            return AnimFactFromBool(held);
        }
        case AnimDerivationOp::Smooth:
        {
            const float input = ReadNumber(values, derivation);
            if (!memory.Seen || derivation.WindowMs <= 0.0f)
            {
                memory.Value = input;
            }
            else
            {
                const double alpha =
                    1.0 - std::exp(-(tickSeconds * 1000.0) / static_cast<double>(derivation.WindowMs));
                memory.Value += static_cast<float>((input - memory.Value) * alpha);
            }
            return AnimFactFromFloat(memory.Value);
        }
        case AnimDerivationOp::Compare:
            return AnimFactFromBool(
                Compare(derivation.Compare, ReadNumber(values, derivation), derivation.Constant));
        case AnimDerivationOp::And:
        {
            bool all = true;
            for (std::size_t i = 0; i < derivation.SourceCount; ++i)
                all = all && ReadBool(values, derivation.Sources[i]);
            return AnimFactFromBool(all);
        }
        case AnimDerivationOp::Or:
        {
            bool any = false;
            for (std::size_t i = 0; i < derivation.SourceCount; ++i)
                any = any || ReadBool(values, derivation.Sources[i]);
            return AnimFactFromBool(any);
        }
        case AnimDerivationOp::Not:
            return AnimFactFromBool(!ReadBool(values, derivation.Sources[0]));
        }
        return 0u;
    }
}

void BeginAnimFactObservation(const AnimBoundRig& rig, AnimFactHistory& history, AnimTick now)
{
    if (history.BindingGeneration == rig.Generation && history.ObservedSinceTick != kAnimNoTick)
        return;
    for (AnimDerivationMemory& memory : history.Derivations)
        memory = AnimDerivationMemory{};
    history.ObservedSinceTick = now;
    history.BindingGeneration = rig.Generation;
}

void EvaluateAnimDerivations(const AnimBoundRig& rig,
                             std::span<std::uint32_t> values,
                             AnimFactHistory& history,
                             AnimTick now,
                             double tickSeconds)
{
    BeginAnimFactObservation(rig, history, now);
    const std::size_t count = std::min(rig.Derivations.size(), kAnimMaxDerivations);
    for (std::size_t i = 0; i < count; ++i)
    {
        const AnimBoundDerivation& derivation = rig.Derivations[i];
        if (derivation.Result >= values.size())
            continue;
        AnimDerivationMemory& memory = history.Derivations[i];
        values[derivation.Result] = Evaluate(derivation, values, memory, now, tickSeconds);
        memory.Seen = true;
    }
}

bool AreAnimDerivedFactsExact(const AnimBoundRig& rig,
                              const AnimFactHistory& history,
                              AnimTick now,
                              double tickSeconds)
{
    if (!rig.HasTemporalDerivations)
        return true;
    if (history.ObservedSinceTick == kAnimNoTick || history.BindingGeneration != rig.Generation
        || now < history.ObservedSinceTick)
    {
        return false;
    }
    return ElapsedMs(history.ObservedSinceTick, now, tickSeconds) >= rig.HorizonMs;
}
