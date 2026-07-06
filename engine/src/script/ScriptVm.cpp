#include "script/ScriptVm.h"

#include "script/ScriptBytecode.h"
#include "script/ScriptIntrinsics.h"

#include <bit>
#include <cassert>
#include <cmath>
#include <cstring>

namespace
{
    [[nodiscard]] double AsF(uint64_t bits) { return std::bit_cast<double>(bits); }
    [[nodiscard]] uint64_t FromF(double v) { return std::bit_cast<uint64_t>(v); }
    [[nodiscard]] int32_t AsI(uint64_t bits) { return static_cast<int32_t>(bits); }
    [[nodiscard]] uint64_t FromI(int32_t v)
    {
        return static_cast<uint64_t>(static_cast<uint32_t>(v));
    }
    [[nodiscard]] int64_t AsL(uint64_t bits) { return static_cast<int64_t>(bits); }
    [[nodiscard]] uint64_t FromL(int64_t v) { return static_cast<uint64_t>(v); }
    [[nodiscard]] uint64_t FromB(bool v) { return v ? 1u : 0u; }

    struct Frame
    {
        uint32_t FunctionIndex = 0;
        uint32_t Pc = 0;   // word offset within the function
        uint32_t Base = 0; // slot arena offset
        uint32_t ReturnSlot = 0; // absolute arena offset for a RETV value
    };
}

ScriptInvokeResult ScriptVm::Invoke(const ScriptModule& module, uint32_t functionIndex,
                                    ScriptHostCallHandler& host,
                                    const ScriptInvokeOptions& options)
{
    assert(functionIndex < module.Functions.size());

    ScriptInvokeResult result;
    Slots.assign(kScriptMaxCallDepth * kScriptMaxFrameSlots, 0);

    Frame frames[kScriptMaxCallDepth];
    uint32_t depth = 1;
    frames[0] = Frame{functionIndex, 0, 0, 0};

    {
        const ScriptFunctionDef& fn = module.Functions[functionIndex];
        assert(options.Args.size() == fn.ArgSlots);
        assert(options.Return.size() == fn.RetSlots);
        std::memcpy(Slots.data(), options.Args.data(), options.Args.size() * sizeof(uint64_t));
    }

    uint64_t budget = options.Budget;

    auto trap = [&result, &frames, &depth](ScriptTrapCode code, uint32_t at) {
        result.Trap = code;
        result.TrapFunction = frames[depth - 1].FunctionIndex;
        result.TrapCodeOffset = at;
    };

    while (true)
    {
        Frame& frame = frames[depth - 1];
        const ScriptFunctionDef& fn = module.Functions[frame.FunctionIndex];
        const uint32_t at = frame.Pc;
        const uint32_t word = module.Code[fn.CodeOffset + at];
        const ScriptOp op = ScriptDecodeOp(word);
        const bool extended = (op == ScriptOp::CldX || op == ScriptOp::CstX
                               || op == ScriptOp::AGetL || op == ScriptOp::ASetL);
        const uint32_t word2 = extended ? module.Code[fn.CodeOffset + at + 1] : 0;
        const uint32_t after = at + (extended ? 2u : 1u);

        if (budget == 0)
        {
            trap(ScriptTrapCode::Budget, at);
            return result;
        }
        --budget;
        ++result.InstructionsExecuted;

        uint64_t* r = Slots.data() + frame.Base;
        const uint8_t a = ScriptDecodeA(word);
        const uint8_t b = ScriptDecodeB(word);
        const uint8_t c = ScriptDecodeC(word);

        frame.Pc = after;

        switch (op)
        {
        case ScriptOp::Nop:
            break;
        case ScriptOp::Mov:
            r[a] = r[b];
            break;
        case ScriptOp::MovW:
            std::memmove(r + a, r + b, static_cast<size_t>(c) * sizeof(uint64_t));
            break;
        case ScriptOp::Ldc:
            r[a] = module.Constants[ScriptDecodeBx(word)];
            break;
        case ScriptOp::Ldi:
            r[a] = FromI(ScriptDecodeSBx(word));
            break;
        case ScriptOp::Ldp:
        {
            // Params flatten to a slot list in declaration order; the
            // instance-resolved block replaces this in Milestone 5. Defaults
            // execute here so hand-assembled modules run standalone.
            uint32_t slot = ScriptDecodeBx(word);
            uint64_t value = 0;
            for (const ScriptParamDef& param : module.Params)
            {
                if (slot < param.SlotCount)
                {
                    value = slot < param.DefaultRaw.size() ? param.DefaultRaw[slot] : 0;
                    break;
                }
                slot -= param.SlotCount;
            }
            r[a] = value;
            break;
        }
        case ScriptOp::AddI:
            r[a] = FromI(static_cast<int32_t>(static_cast<uint32_t>(AsI(r[b]))
                                              + static_cast<uint32_t>(AsI(r[c]))));
            break;
        case ScriptOp::SubI:
            r[a] = FromI(static_cast<int32_t>(static_cast<uint32_t>(AsI(r[b]))
                                              - static_cast<uint32_t>(AsI(r[c]))));
            break;
        case ScriptOp::MulI:
            r[a] = FromI(static_cast<int32_t>(static_cast<uint32_t>(AsI(r[b]))
                                              * static_cast<uint32_t>(AsI(r[c]))));
            break;
        case ScriptOp::DivI:
        {
            const int32_t num = AsI(r[b]);
            const int32_t den = AsI(r[c]);
            if (den == 0)
            {
                trap(ScriptTrapCode::DivZero, at);
                return result;
            }
            // INT_MIN / -1 wraps to INT_MIN rather than trapping (spec D.2).
            const int64_t wide = static_cast<int64_t>(num) / den;
            r[a] = FromI(static_cast<int32_t>(wide));
            break;
        }
        case ScriptOp::ModI:
        {
            const int32_t num = AsI(r[b]);
            const int32_t den = AsI(r[c]);
            if (den == 0)
            {
                trap(ScriptTrapCode::DivZero, at);
                return result;
            }
            const int64_t wide = static_cast<int64_t>(num) % den;
            r[a] = FromI(static_cast<int32_t>(wide));
            break;
        }
        case ScriptOp::NegI:
            r[a] = FromI(static_cast<int32_t>(0u - static_cast<uint32_t>(AsI(r[b]))));
            break;
        case ScriptOp::AddL:
            r[a] = r[b] + r[c];
            break;
        case ScriptOp::SubL:
            r[a] = r[b] - r[c];
            break;
        case ScriptOp::AddF:
            r[a] = FromF(AsF(r[b]) + AsF(r[c]));
            break;
        case ScriptOp::SubF:
            r[a] = FromF(AsF(r[b]) - AsF(r[c]));
            break;
        case ScriptOp::MulF:
            r[a] = FromF(AsF(r[b]) * AsF(r[c]));
            break;
        case ScriptOp::DivF:
            r[a] = FromF(AsF(r[b]) / AsF(r[c]));
            break;
        case ScriptOp::NegF:
            r[a] = FromF(-AsF(r[b]));
            break;
        case ScriptOp::I2F:
            r[a] = FromF(static_cast<double>(AsI(r[b])));
            break;
        case ScriptOp::L2F:
            r[a] = FromF(static_cast<double>(AsL(r[b])));
            break;
        case ScriptOp::F2I:
        {
            const double v = std::trunc(AsF(r[b]));
            if (!(v >= -2147483648.0 && v <= 2147483647.0))
            {
                trap(ScriptTrapCode::Conv, at);
                return result;
            }
            r[a] = FromI(static_cast<int32_t>(v));
            break;
        }
        case ScriptOp::I2L:
            r[a] = FromL(static_cast<int64_t>(AsI(r[b])));
            break;
        case ScriptOp::L2I:
            r[a] = FromI(static_cast<int32_t>(static_cast<uint32_t>(r[b] & 0xFFFFFFFFu)));
            break;
        case ScriptOp::RndF32:
            r[a] = FromF(static_cast<double>(static_cast<float>(AsF(r[b]))));
            break;
        case ScriptOp::CEqI:
            r[a] = FromB(AsI(r[b]) == AsI(r[c]));
            break;
        case ScriptOp::CLtI:
            r[a] = FromB(AsI(r[b]) < AsI(r[c]));
            break;
        case ScriptOp::CLeI:
            r[a] = FromB(AsI(r[b]) <= AsI(r[c]));
            break;
        case ScriptOp::CEqL:
            r[a] = FromB(AsL(r[b]) == AsL(r[c]));
            break;
        case ScriptOp::CLtL:
            r[a] = FromB(AsL(r[b]) < AsL(r[c]));
            break;
        case ScriptOp::CLeL:
            r[a] = FromB(AsL(r[b]) <= AsL(r[c]));
            break;
        case ScriptOp::CEqF:
            r[a] = FromB(AsF(r[b]) == AsF(r[c]));
            break;
        case ScriptOp::CLtF:
            r[a] = FromB(AsF(r[b]) < AsF(r[c]));
            break;
        case ScriptOp::CLeF:
            r[a] = FromB(AsF(r[b]) <= AsF(r[c]));
            break;
        case ScriptOp::CEqE:
            r[a] = FromB(r[b] == r[c]);
            break;
        case ScriptOp::CEqT:
            r[a] = FromB(static_cast<uint32_t>(r[b]) == static_cast<uint32_t>(r[c]));
            break;
        case ScriptOp::Not:
            r[a] = FromB(r[b] == 0);
            break;
        case ScriptOp::Jmp:
            frame.Pc = static_cast<uint32_t>(static_cast<int64_t>(after) + ScriptDecodeSAx(word));
            break;
        case ScriptOp::Bt:
            if (r[a] != 0)
            {
                frame.Pc = static_cast<uint32_t>(static_cast<int64_t>(after) + ScriptDecodeSBx(word));
            }
            break;
        case ScriptOp::Bf:
            if (r[a] == 0)
            {
                frame.Pc = static_cast<uint32_t>(static_cast<int64_t>(after) + ScriptDecodeSBx(word));
            }
            break;
        case ScriptOp::VAdd3:
            r[a + 0] = FromF(AsF(r[b + 0]) + AsF(r[c + 0]));
            r[a + 1] = FromF(AsF(r[b + 1]) + AsF(r[c + 1]));
            r[a + 2] = FromF(AsF(r[b + 2]) + AsF(r[c + 2]));
            break;
        case ScriptOp::VSub3:
            r[a + 0] = FromF(AsF(r[b + 0]) - AsF(r[c + 0]));
            r[a + 1] = FromF(AsF(r[b + 1]) - AsF(r[c + 1]));
            r[a + 2] = FromF(AsF(r[b + 2]) - AsF(r[c + 2]));
            break;
        case ScriptOp::VScale3:
        {
            const double s = AsF(r[c]);
            r[a + 0] = FromF(AsF(r[b + 0]) * s);
            r[a + 1] = FromF(AsF(r[b + 1]) * s);
            r[a + 2] = FromF(AsF(r[b + 2]) * s);
            break;
        }
        case ScriptOp::VDot3:
        {
            // Fixed order per the spec: ((x*x' + y*y') + z*z').
            const double v = (AsF(r[b + 0]) * AsF(r[c + 0]) + AsF(r[b + 1]) * AsF(r[c + 1]))
                             + AsF(r[b + 2]) * AsF(r[c + 2]);
            r[a] = FromF(v);
            break;
        }
        case ScriptOp::VLen3:
        {
            const double v = (AsF(r[b + 0]) * AsF(r[b + 0]) + AsF(r[b + 1]) * AsF(r[b + 1]))
                             + AsF(r[b + 2]) * AsF(r[b + 2]);
            r[a] = FromF(std::sqrt(v));
            break;
        }
        case ScriptOp::Cld:
        {
            const uint8_t slots = module.FieldBinds[c].SlotCount;
            const ScriptTrapCode code =
                host.ComponentLoad(module, r[b], c, 0, std::span<uint64_t>(r + a, slots));
            if (code != ScriptTrapCode::None)
            {
                trap(code, at);
                return result;
            }
            break;
        }
        case ScriptOp::Cst:
        {
            const uint8_t slots = module.FieldBinds[c].SlotCount;
            const ScriptTrapCode code =
                host.ComponentStore(module, r[a], c, 0, std::span<const uint64_t>(r + b, slots));
            if (code != ScriptTrapCode::None)
            {
                trap(code, at);
                return result;
            }
            break;
        }
        case ScriptOp::CHas:
        {
            bool has = false;
            const ScriptTrapCode code = host.ComponentHas(module, r[b], c, has);
            if (code != ScriptTrapCode::None)
            {
                trap(code, at);
                return result;
            }
            r[a] = FromB(has);
            break;
        }
        case ScriptOp::CldX:
        case ScriptOp::CstX:
        {
            const uint16_t bind = ScriptDecodeExtFieldBind(word2);
            const uint8_t indexReg = ScriptDecodeExtIndexReg(word2);
            const int32_t index = AsI(r[indexReg]);
            if (index < 0)
            {
                trap(ScriptTrapCode::Bounds, at);
                return result;
            }
            const uint8_t slots = module.FieldBinds[bind].SlotCount;
            const ScriptTrapCode code =
                (op == ScriptOp::CldX)
                    ? host.ComponentLoad(module, r[b], bind, static_cast<uint32_t>(index),
                                         std::span<uint64_t>(r + a, slots))
                    : host.ComponentStore(module, r[a], bind, static_cast<uint32_t>(index),
                                          std::span<const uint64_t>(r + b, slots));
            if (code != ScriptTrapCode::None)
            {
                trap(code, at);
                return result;
            }
            break;
        }
        case ScriptOp::THas:
        case ScriptOp::THasU:
        {
            bool has = false;
            const ScriptTrapCode code =
                host.TagHas(module, r[b], c, op == ScriptOp::THasU, has);
            if (code != ScriptTrapCode::None)
            {
                trap(code, at);
                return result;
            }
            r[a] = FromB(has);
            break;
        }
        case ScriptOp::TAdd:
        {
            const ScriptTrapCode code = host.TagAdd(module, r[a], b);
            if (code != ScriptTrapCode::None)
            {
                trap(code, at);
                return result;
            }
            break;
        }
        case ScriptOp::TRem:
        {
            const ScriptTrapCode code = host.TagRemove(module, r[a], b);
            if (code != ScriptTrapCode::None)
            {
                trap(code, at);
                return result;
            }
            break;
        }
        case ScriptOp::HCall:
        {
            // Intrinsics dispatch by import name; a per-module resolved
            // import table replaces the lookup at link time (Milestone 4).
            const ScriptHostImport& import = module.HostImports[b];
            const int32_t intrinsic = FindScriptIntrinsic(module.GetString(import.Name));
            const ScriptTrapCode code =
                (intrinsic >= 0)
                    ? ExecuteScriptIntrinsic(intrinsic, r + a)
                    : host.HostCall(module, b, std::span<uint64_t>(r + a, kScriptMaxFrameSlots - a), c);
            if (code != ScriptTrapCode::None)
            {
                trap(code, at);
                return result;
            }
            break;
        }
        case ScriptOp::AGetL:
        case ScriptOp::ASetL:
        {
            const uint16_t count = ScriptDecodeExtElementCount(word2);
            const uint8_t indexReg = ScriptDecodeExtIndexReg(word2);
            const uint8_t eltSlots = ScriptDecodeExtElementSlots(word2);
            const int32_t index = AsI(r[indexReg]);
            if (index < 0 || static_cast<uint32_t>(index) >= count)
            {
                trap(ScriptTrapCode::Bounds, at);
                return result;
            }
            const uint32_t offset = static_cast<uint32_t>(index) * eltSlots;
            if (op == ScriptOp::AGetL)
            {
                std::memmove(r + a, r + b + offset, eltSlots * sizeof(uint64_t));
            }
            else
            {
                std::memmove(r + a + offset, r + b, eltSlots * sizeof(uint64_t));
            }
            break;
        }
        case ScriptOp::Call:
        {
            if (depth >= kScriptMaxCallDepth)
            {
                trap(ScriptTrapCode::CallDepth, at);
                return result;
            }
            const uint16_t callee = ScriptDecodeBx(word);
            const ScriptFunctionDef& calleeFn = module.Functions[callee];
            Frame& next = frames[depth];
            next.FunctionIndex = callee;
            next.Pc = 0;
            next.Base = depth * kScriptMaxFrameSlots;
            next.ReturnSlot = frame.Base + a;
            std::memcpy(Slots.data() + next.Base, r + a,
                        calleeFn.ArgSlots * sizeof(uint64_t));
            ++depth;
            break;
        }
        case ScriptOp::Enter:
            result.PendingState = ScriptDecodeSAx(word);
            break;
        case ScriptOp::Ret:
        case ScriptOp::RetV:
        {
            if (op == ScriptOp::RetV)
            {
                const uint64_t* value = r + a;
                if (depth == 1)
                {
                    std::memcpy(options.Return.data(), value, b * sizeof(uint64_t));
                }
                else
                {
                    std::memcpy(Slots.data() + frame.ReturnSlot, value, b * sizeof(uint64_t));
                }
            }
            --depth;
            if (depth == 0)
            {
                return result;
            }
            break;
        }
        }
    }
}
