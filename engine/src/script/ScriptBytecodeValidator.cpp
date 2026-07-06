#include "script/ScriptBytecodeValidator.h"

#include "script/ScriptBytecode.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <vector>

namespace
{
    // Slot types for the flow pass. Invalid = not defined on every path to
    // this point; Unknown = defined but untyped (LDC results, free-function
    // arguments); the rest are concrete.
    enum class SlotType : uint8_t
    {
        Invalid,
        Unknown,
        Bool,
        I32,
        I64,
        F64,
        Entity,
        Tag,
    };

    struct FnContext
    {
        const ScriptModule* Module = nullptr;
        uint32_t FunctionIndex = 0;
        const ScriptFunctionDef* Fn = nullptr;
        // Instruction-start flags per word offset (extension words are not
        // starts), filled by the structural pass and reused by the flow pass.
        std::vector<bool> IsInstructionStart;
        // Owning declaration, if any callback references this function.
        const ScriptDeclaration* Decl = nullptr;
        uint32_t ParamSlotTotal = 0;
        std::vector<SlotType> ParamSlotTypes;
    };

    struct Failure
    {
        const char* Rule;
        std::string Message;
        uint32_t CodeOffset;
    };

    using MaybeFailure = std::optional<Failure>;

    [[nodiscard]] SlotType SlotTypeFromScalar(uint8_t scalar)
    {
        switch (static_cast<ScriptScalarKind>(scalar))
        {
        case ScriptScalarKind::Bool:   return SlotType::Bool;
        case ScriptScalarKind::Int32:  return SlotType::I32;
        case ScriptScalarKind::UInt32: return SlotType::I32;
        case ScriptScalarKind::Float:  return SlotType::F64;
        case ScriptScalarKind::Double: return SlotType::F64;
        case ScriptScalarKind::Color3: return SlotType::F64;
        case ScriptScalarKind::Entity: return SlotType::Entity;
        case ScriptScalarKind::TagId:  return SlotType::Tag;
        case ScriptScalarKind::Int64:  return SlotType::I64;
        }
        return SlotType::Unknown;
    }

    // Spec section C signature letters to slot type sequences. Returns false
    // on a malformed signature string.
    bool AppendSignatureSlots(char c, const char* next, int& consumedExtra,
                              std::vector<SlotType>& out, bool& variadicImage)
    {
        consumedExtra = 0;
        switch (c)
        {
        case 'b': out.push_back(SlotType::Bool); return true;
        case 'i': out.push_back(SlotType::I32); return true;
        case 'l': out.push_back(SlotType::I64); return true;
        case 'f': out.push_back(SlotType::F64); return true;
        case 'd': out.push_back(SlotType::F64); return true;
        case 'e': out.push_back(SlotType::Entity); return true;
        case 't': out.push_back(SlotType::Tag); return true;
        case 'A': out.push_back(SlotType::I32); return true; // linked asset id slot
        case '2': out.insert(out.end(), 2, SlotType::F64); return true;
        case '3': out.insert(out.end(), 3, SlotType::F64); return true;
        case 'q': out.insert(out.end(), 4, SlotType::F64); return true;
        case 'C':
            // Component-bind index followed by a component image whose width
            // depends on the bound component; the image slots are checked
            // for defined-ness only.
            out.push_back(SlotType::I32);
            variadicImage = true;
            return true;
        case 'S':
            if (next == nullptr)
            {
                return false;
            }
            consumedExtra = 1;
            switch (*next)
            {
            case '1': // RayHit
            case '2': // SweepHit
                out.push_back(SlotType::Bool);
                out.push_back(SlotType::Entity);
                out.insert(out.end(), 3, SlotType::F64);
                out.insert(out.end(), 3, SlotType::F64);
                out.push_back(SlotType::F64);
                return true;
            case '3': // OverlapHits
                out.push_back(SlotType::I32);
                out.insert(out.end(), 16, SlotType::Entity);
                return true;
            default:
                return false;
            }
        default:
            return false;
        }
    }

    struct HostSignature
    {
        std::vector<SlotType> Return;
        std::vector<SlotType> Args;
        bool VariadicImage = false;
    };

    [[nodiscard]] bool ParseHostSignature(std::string_view text, HostSignature& out)
    {
        size_t i = 0;
        auto parseSeq = [&text, &i](std::vector<SlotType>& slots, bool& variadic, bool single) -> bool {
            while (i < text.size() && text[i] != ' ')
            {
                const char c = text[i];
                if (c == 'v')
                {
                    if (!slots.empty())
                    {
                        return false;
                    }
                    ++i;
                    break;
                }
                const char* next = (i + 1 < text.size()) ? &text[i + 1] : nullptr;
                int extra = 0;
                if (!AppendSignatureSlots(c, next, extra, slots, variadic))
                {
                    return false;
                }
                i += 1 + static_cast<size_t>(extra);
                if (single)
                {
                    break;
                }
            }
            return true;
        };

        bool retVariadic = false;
        if (!parseSeq(out.Return, retVariadic, /*single*/ true) || retVariadic)
        {
            return false;
        }
        if (i < text.size())
        {
            if (text[i] != ' ')
            {
                return false;
            }
            ++i;
            if (!parseSeq(out.Args, out.VariadicImage, /*single*/ false))
            {
                return false;
            }
        }
        return i == text.size();
    }

    // Register preludes per declaration kind (spec section C).
    [[nodiscard]] std::vector<SlotType> PreludeFor(ScriptDeclKind kind)
    {
        switch (kind)
        {
        case ScriptDeclKind::Ability:
            return { SlotType::Entity, SlotType::I64, SlotType::F64,
                     SlotType::F64, SlotType::F64, SlotType::F64,
                     SlotType::F64, SlotType::F64, SlotType::F64 };
        case ScriptDeclKind::Behavior:
            return { SlotType::Entity, SlotType::I64, SlotType::F64 };
        case ScriptDeclKind::Trigger:
        case ScriptDeclKind::Interaction:
            return { SlotType::Entity, SlotType::Entity, SlotType::I64, SlotType::F64 };
        }
        return {};
    }

    //=========================================================================
    // Structural pass (rules 1-6)
    //=========================================================================

    MaybeFailure StructuralPass(FnContext& ctx)
    {
        const ScriptModule& module = *ctx.Module;
        const ScriptFunctionDef& fn = *ctx.Fn;

        if (fn.CodeOffset > module.Code.size()
            || static_cast<uint64_t>(fn.CodeOffset) + fn.WordCount > module.Code.size()
            || fn.WordCount == 0)
        {
            return Failure{"structural-1", "function code range outside the Code section", 0};
        }
        if (fn.FrameSlots == 0 && fn.ArgSlots > 0)
        {
            return Failure{"structural-5", "argument slots exceed the frame", 0};
        }

        ctx.IsInstructionStart.assign(fn.WordCount, false);

        // First walk: decode, mark instruction starts, check EXT bounds.
        for (uint32_t at = 0; at < fn.WordCount;)
        {
            const uint32_t word = module.Code[fn.CodeOffset + at];
            const ScriptOpInfo* info = FindScriptOp(ScriptDecodeOpRaw(word));
            if (info == nullptr)
            {
                return Failure{"structural-1", "unassigned opcode", at};
            }
            ctx.IsInstructionStart[at] = true;
            const uint32_t width = (info->Format == ScriptOpFormat::Ext) ? 2u : 1u;
            if (at + width > fn.WordCount)
            {
                return Failure{"structural-1", "extension word runs past the function", at};
            }
            at += width;
        }

        // Second walk: per-instruction structural rules.
        for (uint32_t at = 0; at < fn.WordCount;)
        {
            const uint32_t word = module.Code[fn.CodeOffset + at];
            const ScriptOpInfo* info = FindScriptOp(ScriptDecodeOpRaw(word));
            const uint32_t width = (info->Format == ScriptOpFormat::Ext) ? 2u : 1u;
            const uint32_t after = at + width;
            const ScriptOp op = info->Op;

            auto checkReg = [&](uint32_t base, uint32_t span) -> bool {
                return span > 0 && base + span <= fn.FrameSlots;
            };
            const uint8_t a = ScriptDecodeA(word);
            const uint8_t b = ScriptDecodeB(word);
            const uint8_t c = ScriptDecodeC(word);

            switch (op)
            {
            case ScriptOp::Jmp:
            case ScriptOp::Bt:
            case ScriptOp::Bf:
            {
                const int32_t offset = (op == ScriptOp::Jmp) ? ScriptDecodeSAx(word)
                                                             : ScriptDecodeSBx(word);
                const int64_t target = static_cast<int64_t>(after) + offset;
                if (target < 0 || target >= fn.WordCount
                    || !ctx.IsInstructionStart[static_cast<uint32_t>(target)])
                {
                    return Failure{"structural-2", "branch target is not an instruction start in this function", at};
                }
                if (op != ScriptOp::Jmp && !checkReg(a, 1))
                {
                    return Failure{"structural-5", "branch condition register out of frame", at};
                }
                break;
            }
            case ScriptOp::Ldc:
                if (ScriptDecodeBx(word) >= module.Constants.size())
                {
                    return Failure{"structural-3", "constant index out of range", at};
                }
                if (!checkReg(a, 1))
                {
                    return Failure{"structural-5", "register out of frame", at};
                }
                break;
            case ScriptOp::Ldp:
                if (ScriptDecodeBx(word) >= ctx.ParamSlotTotal)
                {
                    return Failure{"structural-3", "param slot index out of range", at};
                }
                if (!checkReg(a, 1))
                {
                    return Failure{"structural-5", "register out of frame", at};
                }
                break;
            case ScriptOp::HCall:
                if (b >= module.HostImports.size())
                {
                    return Failure{"structural-3", "host import index out of range", at};
                }
                if (c > 0 && !checkReg(a, c))
                {
                    return Failure{"structural-5", "host call window out of frame", at};
                }
                break;
            case ScriptOp::Call:
            {
                const uint16_t callee = ScriptDecodeBx(word);
                if (callee >= module.Functions.size())
                {
                    return Failure{"structural-3", "function index out of range", at};
                }
                const uint8_t argSlots = module.Functions[callee].ArgSlots;
                const uint8_t retSlots = module.Functions[callee].RetSlots;
                const uint32_t window = std::max<uint32_t>(argSlots, retSlots);
                if (window > 0 && !checkReg(a, window))
                {
                    return Failure{"structural-5", "call window out of frame", at};
                }
                break;
            }
            case ScriptOp::Cld:
            case ScriptOp::Cst:
                if (c >= module.FieldBinds.size())
                {
                    return Failure{"structural-3", "field bind index out of range", at};
                }
                if (!checkReg(a, 1) || !checkReg(b, 1))
                {
                    return Failure{"structural-5", "register out of frame", at};
                }
                if (op == ScriptOp::Cld
                    ? !checkReg(a, module.FieldBinds[c].SlotCount)
                    : !checkReg(b, module.FieldBinds[c].SlotCount))
                {
                    return Failure{"structural-5", "field slot group out of frame", at};
                }
                break;
            case ScriptOp::CHas:
                if (c >= module.ComponentBinds.size())
                {
                    return Failure{"structural-3", "component bind index out of range", at};
                }
                if (!checkReg(a, 1) || !checkReg(b, 1))
                {
                    return Failure{"structural-5", "register out of frame", at};
                }
                break;
            case ScriptOp::CldX:
            case ScriptOp::CstX:
            {
                const uint32_t word2 = module.Code[fn.CodeOffset + at + 1];
                const uint16_t bind = ScriptDecodeExtFieldBind(word2);
                const uint8_t indexReg = ScriptDecodeExtIndexReg(word2);
                if (bind >= module.FieldBinds.size())
                {
                    return Failure{"structural-3", "field bind index out of range", at};
                }
                if (!checkReg(a, module.FieldBinds[bind].SlotCount) || !checkReg(b, 1)
                    || !checkReg(indexReg, 1))
                {
                    return Failure{"structural-5", "register out of frame", at};
                }
                break;
            }
            case ScriptOp::THas:
            case ScriptOp::THasU:
                if (c >= module.TagBinds.size())
                {
                    return Failure{"structural-3", "tag bind index out of range", at};
                }
                if (!checkReg(a, 1) || !checkReg(b, 1))
                {
                    return Failure{"structural-5", "register out of frame", at};
                }
                break;
            case ScriptOp::TAdd:
            case ScriptOp::TRem:
                if (b >= module.TagBinds.size())
                {
                    return Failure{"structural-3", "tag bind index out of range", at};
                }
                if (!checkReg(a, 1))
                {
                    return Failure{"structural-5", "register out of frame", at};
                }
                break;
            case ScriptOp::AGetL:
            case ScriptOp::ASetL:
            {
                const uint32_t word2 = module.Code[fn.CodeOffset + at + 1];
                const uint16_t count = ScriptDecodeExtElementCount(word2);
                const uint8_t indexReg = ScriptDecodeExtIndexReg(word2);
                const uint8_t eltSlots = ScriptDecodeExtElementSlots(word2);
                if (count == 0 || eltSlots == 0)
                {
                    return Failure{"structural-1", "array op with zero count or element size", at};
                }
                const uint32_t arrayBase = (op == ScriptOp::AGetL) ? b : a;
                const uint32_t valueBase = (op == ScriptOp::AGetL) ? a : b;
                if (!checkReg(arrayBase, static_cast<uint32_t>(count) * eltSlots)
                    || !checkReg(valueBase, eltSlots) || !checkReg(indexReg, 1))
                {
                    return Failure{"structural-5", "array op registers out of frame", at};
                }
                break;
            }
            case ScriptOp::Enter:
            {
                if (ctx.Decl == nullptr
                    || (ctx.Decl->Kind != ScriptDeclKind::Ability
                        && ctx.Decl->Kind != ScriptDeclKind::Behavior))
                {
                    return Failure{"structural-6", "ENTER outside an ability or behavior callback", at};
                }
                const int32_t state = ScriptDecodeSAx(word);
                if (state < 0 || static_cast<size_t>(state) >= ctx.Decl->States.size())
                {
                    return Failure{"structural-3", "state index out of range", at};
                }
                break;
            }
            case ScriptOp::MovW:
                if (c == 0 || !checkReg(a, c) || !checkReg(b, c))
                {
                    return Failure{"structural-5", "wide move out of frame", at};
                }
                break;
            case ScriptOp::VAdd3:
            case ScriptOp::VSub3:
                if (!checkReg(a, 3) || !checkReg(b, 3) || !checkReg(c, 3))
                {
                    return Failure{"structural-5", "vector registers out of frame", at};
                }
                break;
            case ScriptOp::VScale3:
                if (!checkReg(a, 3) || !checkReg(b, 3) || !checkReg(c, 1))
                {
                    return Failure{"structural-5", "vector registers out of frame", at};
                }
                break;
            case ScriptOp::VDot3:
                if (!checkReg(a, 1) || !checkReg(b, 3) || !checkReg(c, 3))
                {
                    return Failure{"structural-5", "vector registers out of frame", at};
                }
                break;
            case ScriptOp::VLen3:
                if (!checkReg(a, 1) || !checkReg(b, 3))
                {
                    return Failure{"structural-5", "vector registers out of frame", at};
                }
                break;
            case ScriptOp::RetV:
                if (b == 0 || !checkReg(a, b))
                {
                    return Failure{"structural-5", "return value out of frame", at};
                }
                break;
            case ScriptOp::Ret:
            case ScriptOp::Nop:
                break;
            default:
                // Remaining ABC ops read/write single slots.
                if (op == ScriptOp::Mov || op == ScriptOp::Not
                    || (op >= ScriptOp::I2F && op <= ScriptOp::RndF32)
                    || op == ScriptOp::NegI || op == ScriptOp::NegF)
                {
                    if (!checkReg(a, 1) || !checkReg(b, 1))
                    {
                        return Failure{"structural-5", "register out of frame", at};
                    }
                }
                else if (op == ScriptOp::Ldi)
                {
                    if (!checkReg(a, 1))
                    {
                        return Failure{"structural-5", "register out of frame", at};
                    }
                }
                else
                {
                    if (!checkReg(a, 1) || !checkReg(b, 1) || !checkReg(c, 1))
                    {
                        return Failure{"structural-5", "register out of frame", at};
                    }
                }
                break;
            }

            // Rule 4: control must not flow off the end of the function.
            const bool terminal = (op == ScriptOp::Ret || op == ScriptOp::RetV
                                   || op == ScriptOp::Jmp);
            if (after == fn.WordCount && !terminal)
            {
                return Failure{"structural-4", "control flow runs off the end of the function", at};
            }
            at = after;
        }
        return std::nullopt;
    }

    //=========================================================================
    // Type-flow pass (rules 7-10)
    //=========================================================================

    struct FlowState
    {
        std::vector<SlotType> Slots;
    };

    // Meet for the merge points. Concrete-type disagreement is an error, per
    // the spec: the compiler inserts explicit conversions so joins agree.
    [[nodiscard]] bool MeetInto(FlowState& into, const FlowState& from, bool& conflict)
    {
        bool changed = false;
        conflict = false;
        for (size_t i = 0; i < into.Slots.size(); ++i)
        {
            const SlotType a = into.Slots[i];
            const SlotType b = from.Slots[i];
            SlotType merged = a;
            if (a == b)
            {
                merged = a;
            }
            else if (a == SlotType::Invalid || b == SlotType::Invalid)
            {
                merged = SlotType::Invalid;
            }
            else if (a == SlotType::Unknown || b == SlotType::Unknown)
            {
                merged = SlotType::Unknown;
            }
            else
            {
                conflict = true;
                return false;
            }
            if (merged != a)
            {
                into.Slots[i] = merged;
                changed = true;
            }
        }
        return changed;
    }

    struct FlowChecker
    {
        const ScriptModule& Module;
        const FnContext& Ctx;
        FlowState State;
        MaybeFailure Error;
        uint32_t At = 0;

        void Fail(const char* rule, std::string message)
        {
            if (!Error)
            {
                Error = Failure{rule, std::move(message), At};
            }
        }

        [[nodiscard]] bool Use(uint32_t base, uint32_t span, SlotType required)
        {
            for (uint32_t i = 0; i < span; ++i)
            {
                const SlotType t = State.Slots[base + i];
                if (t == SlotType::Invalid)
                {
                    Fail("type-7", "use of a slot not defined on every path");
                    return false;
                }
                if (required != SlotType::Unknown && t != SlotType::Unknown && t != required)
                {
                    Fail("type-7", "operand slot has the wrong type");
                    return false;
                }
            }
            return true;
        }

        void Def(uint32_t base, uint32_t span, SlotType type)
        {
            for (uint32_t i = 0; i < span; ++i)
            {
                State.Slots[base + i] = type;
            }
        }
    };

    MaybeFailure TypeFlowPass(FnContext& ctx)
    {
        const ScriptModule& module = *ctx.Module;
        const ScriptFunctionDef& fn = *ctx.Fn;

        FlowState entry;
        entry.Slots.assign(fn.FrameSlots, SlotType::Invalid);
        if (ctx.Decl != nullptr)
        {
            const std::vector<SlotType> prelude = PreludeFor(ctx.Decl->Kind);
            if (fn.ArgSlots != prelude.size())
            {
                return Failure{"type-7", "callback argument slots do not match the declaration kind's prelude", 0};
            }
            for (size_t i = 0; i < prelude.size(); ++i)
            {
                entry.Slots[i] = prelude[i];
            }
        }
        else
        {
            for (uint32_t i = 0; i < fn.ArgSlots; ++i)
            {
                entry.Slots[i] = SlotType::Unknown;
            }
        }

        std::vector<std::optional<FlowState>> inStates(fn.WordCount);
        inStates[0] = entry;
        std::deque<uint32_t> worklist{0};

        auto propagate = [&](uint32_t target, const FlowState& state) -> MaybeFailure {
            if (!inStates[target])
            {
                inStates[target] = state;
                worklist.push_back(target);
                return std::nullopt;
            }
            bool conflict = false;
            const bool changed = MeetInto(*inStates[target], state, conflict);
            if (conflict)
            {
                return Failure{"type-7", "concrete slot types disagree at a join", target};
            }
            if (changed)
            {
                worklist.push_back(target);
            }
            return std::nullopt;
        };

        while (!worklist.empty())
        {
            const uint32_t at = worklist.front();
            worklist.pop_front();

            FlowChecker check{module, ctx, *inStates[at], std::nullopt, at};
            const uint32_t word = module.Code[fn.CodeOffset + at];
            const ScriptOpInfo* info = FindScriptOp(ScriptDecodeOpRaw(word));
            const uint32_t width = (info->Format == ScriptOpFormat::Ext) ? 2u : 1u;
            const uint32_t after = at + width;
            const ScriptOp op = info->Op;
            const uint8_t a = ScriptDecodeA(word);
            const uint8_t b = ScriptDecodeB(word);
            const uint8_t c = ScriptDecodeC(word);

            bool fallsThrough = true;

            switch (op)
            {
            case ScriptOp::Nop:
                break;
            case ScriptOp::Mov:
                if (check.Use(b, 1, SlotType::Unknown))
                {
                    check.Def(a, 1, check.State.Slots[b]);
                }
                break;
            case ScriptOp::MovW:
                if (check.Use(b, c, SlotType::Unknown))
                {
                    for (uint32_t i = 0; i < c; ++i)
                    {
                        check.State.Slots[a + i] = check.State.Slots[b + i];
                    }
                }
                break;
            case ScriptOp::Ldc:
                check.Def(a, 1, SlotType::Unknown);
                break;
            case ScriptOp::Ldi:
                check.Def(a, 1, SlotType::I32);
                break;
            case ScriptOp::Ldp:
                check.Def(a, 1, ctx.ParamSlotTypes[ScriptDecodeBx(word)]);
                break;
            case ScriptOp::AddI: case ScriptOp::SubI: case ScriptOp::MulI:
            case ScriptOp::DivI: case ScriptOp::ModI:
                if (check.Use(b, 1, SlotType::I32) && check.Use(c, 1, SlotType::I32))
                {
                    check.Def(a, 1, SlotType::I32);
                }
                break;
            case ScriptOp::NegI:
                if (check.Use(b, 1, SlotType::I32))
                {
                    check.Def(a, 1, SlotType::I32);
                }
                break;
            case ScriptOp::AddL: case ScriptOp::SubL:
                if (check.Use(b, 1, SlotType::I64) && check.Use(c, 1, SlotType::I64))
                {
                    check.Def(a, 1, SlotType::I64);
                }
                break;
            case ScriptOp::AddF: case ScriptOp::SubF: case ScriptOp::MulF:
            case ScriptOp::DivF:
                if (check.Use(b, 1, SlotType::F64) && check.Use(c, 1, SlotType::F64))
                {
                    check.Def(a, 1, SlotType::F64);
                }
                break;
            case ScriptOp::NegF:
            case ScriptOp::RndF32:
                if (check.Use(b, 1, SlotType::F64))
                {
                    check.Def(a, 1, SlotType::F64);
                }
                break;
            case ScriptOp::I2F:
                if (check.Use(b, 1, SlotType::I32)) check.Def(a, 1, SlotType::F64);
                break;
            case ScriptOp::L2F:
                if (check.Use(b, 1, SlotType::I64)) check.Def(a, 1, SlotType::F64);
                break;
            case ScriptOp::F2I:
                if (check.Use(b, 1, SlotType::F64)) check.Def(a, 1, SlotType::I32);
                break;
            case ScriptOp::I2L:
                if (check.Use(b, 1, SlotType::I32)) check.Def(a, 1, SlotType::I64);
                break;
            case ScriptOp::L2I:
                if (check.Use(b, 1, SlotType::I64)) check.Def(a, 1, SlotType::I32);
                break;
            case ScriptOp::CEqI: case ScriptOp::CLtI: case ScriptOp::CLeI:
                if (check.Use(b, 1, SlotType::I32) && check.Use(c, 1, SlotType::I32))
                {
                    check.Def(a, 1, SlotType::Bool);
                }
                break;
            case ScriptOp::CEqL: case ScriptOp::CLtL: case ScriptOp::CLeL:
                if (check.Use(b, 1, SlotType::I64) && check.Use(c, 1, SlotType::I64))
                {
                    check.Def(a, 1, SlotType::Bool);
                }
                break;
            case ScriptOp::CEqF: case ScriptOp::CLtF: case ScriptOp::CLeF:
                if (check.Use(b, 1, SlotType::F64) && check.Use(c, 1, SlotType::F64))
                {
                    check.Def(a, 1, SlotType::Bool);
                }
                break;
            case ScriptOp::CEqE:
                if (check.Use(b, 1, SlotType::Entity) && check.Use(c, 1, SlotType::Entity))
                {
                    check.Def(a, 1, SlotType::Bool);
                }
                break;
            case ScriptOp::CEqT:
                if (check.Use(b, 1, SlotType::Tag) && check.Use(c, 1, SlotType::Tag))
                {
                    check.Def(a, 1, SlotType::Bool);
                }
                break;
            case ScriptOp::Not:
                if (check.Use(b, 1, SlotType::Bool))
                {
                    check.Def(a, 1, SlotType::Bool);
                }
                break;
            case ScriptOp::Jmp:
                fallsThrough = false;
                break;
            case ScriptOp::Bt:
            case ScriptOp::Bf:
                (void)check.Use(a, 1, SlotType::Bool);
                break;
            case ScriptOp::VAdd3: case ScriptOp::VSub3:
                if (check.Use(b, 3, SlotType::F64) && check.Use(c, 3, SlotType::F64))
                {
                    check.Def(a, 3, SlotType::F64);
                }
                break;
            case ScriptOp::VScale3:
                if (check.Use(b, 3, SlotType::F64) && check.Use(c, 1, SlotType::F64))
                {
                    check.Def(a, 3, SlotType::F64);
                }
                break;
            case ScriptOp::VDot3:
                if (check.Use(b, 3, SlotType::F64) && check.Use(c, 3, SlotType::F64))
                {
                    check.Def(a, 1, SlotType::F64);
                }
                break;
            case ScriptOp::VLen3:
                if (check.Use(b, 3, SlotType::F64))
                {
                    check.Def(a, 1, SlotType::F64);
                }
                break;
            case ScriptOp::Cld:
            {
                const ScriptFieldBind& bind = module.FieldBinds[c];
                if (check.Use(b, 1, SlotType::Entity))
                {
                    check.Def(a, bind.SlotCount, SlotTypeFromScalar(bind.Scalar));
                }
                break;
            }
            case ScriptOp::Cst:
            {
                const ScriptFieldBind& bind = module.FieldBinds[c];
                (void)(check.Use(a, 1, SlotType::Entity)
                       && check.Use(b, bind.SlotCount, SlotTypeFromScalar(bind.Scalar)));
                break;
            }
            case ScriptOp::CHas:
                if (check.Use(b, 1, SlotType::Entity))
                {
                    check.Def(a, 1, SlotType::Bool);
                }
                break;
            case ScriptOp::CldX:
            case ScriptOp::CstX:
            {
                const uint32_t word2 = module.Code[fn.CodeOffset + at + 1];
                const ScriptFieldBind& bind = module.FieldBinds[ScriptDecodeExtFieldBind(word2)];
                const uint8_t indexReg = ScriptDecodeExtIndexReg(word2);
                const SlotType eltType = SlotTypeFromScalar(bind.Scalar);
                if (op == ScriptOp::CldX)
                {
                    if (check.Use(b, 1, SlotType::Entity) && check.Use(indexReg, 1, SlotType::I32))
                    {
                        check.Def(a, bind.SlotCount, eltType);
                    }
                }
                else
                {
                    (void)(check.Use(a, 1, SlotType::Entity)
                           && check.Use(indexReg, 1, SlotType::I32)
                           && check.Use(b, bind.SlotCount, eltType));
                }
                break;
            }
            case ScriptOp::THas:
            case ScriptOp::THasU:
                if (check.Use(b, 1, SlotType::Entity))
                {
                    check.Def(a, 1, SlotType::Bool);
                }
                break;
            case ScriptOp::TAdd:
            case ScriptOp::TRem:
                (void)check.Use(a, 1, SlotType::Entity);
                break;
            case ScriptOp::HCall:
            {
                const ScriptHostImport& import = module.HostImports[b];
                HostSignature signature;
                if (!ParseHostSignature(module.GetString(import.Signature), signature))
                {
                    check.Fail("type-8", "malformed host import signature");
                    break;
                }
                if (signature.VariadicImage
                    ? c < signature.Args.size()
                    : c != signature.Args.size())
                {
                    check.Fail("type-8", "host call window does not match the import signature");
                    break;
                }
                bool ok = true;
                for (size_t i = 0; i < signature.Args.size() && ok; ++i)
                {
                    ok = check.Use(a + static_cast<uint32_t>(i), 1, signature.Args[i]);
                }
                // Image slots past the typed prefix: defined-ness only.
                for (uint32_t i = static_cast<uint32_t>(signature.Args.size()); i < c && ok; ++i)
                {
                    ok = check.Use(a + i, 1, SlotType::Unknown);
                }
                if (ok)
                {
                    // Consumed window becomes invalid, then the return value
                    // lands at the base.
                    check.Def(a, c, SlotType::Invalid);
                    for (size_t i = 0; i < signature.Return.size(); ++i)
                    {
                        check.State.Slots[a + i] = signature.Return[i];
                    }
                }
                break;
            }
            case ScriptOp::AGetL:
            case ScriptOp::ASetL:
            {
                const uint32_t word2 = module.Code[fn.CodeOffset + at + 1];
                const uint16_t count = ScriptDecodeExtElementCount(word2);
                const uint8_t indexReg = ScriptDecodeExtIndexReg(word2);
                const uint8_t eltSlots = ScriptDecodeExtElementSlots(word2);
                const uint32_t arrayBase = (op == ScriptOp::AGetL) ? b : a;
                const uint32_t valueBase = (op == ScriptOp::AGetL) ? a : b;
                if (op == ScriptOp::AGetL)
                {
                    if (check.Use(arrayBase, static_cast<uint32_t>(count) * eltSlots, SlotType::Unknown)
                        && check.Use(indexReg, 1, SlotType::I32))
                    {
                        check.Def(valueBase, eltSlots, SlotType::Unknown);
                    }
                }
                else
                {
                    (void)(check.Use(valueBase, eltSlots, SlotType::Unknown)
                           && check.Use(indexReg, 1, SlotType::I32));
                    // The stored elements keep whatever type the array held;
                    // local arrays flow as Unknown.
                    check.Def(arrayBase, static_cast<uint32_t>(count) * eltSlots, SlotType::Unknown);
                }
                break;
            }
            case ScriptOp::Call:
            {
                const ScriptFunctionDef& callee = module.Functions[ScriptDecodeBx(word)];
                bool ok = true;
                for (uint32_t i = 0; i < callee.ArgSlots && ok; ++i)
                {
                    ok = check.Use(a + i, 1, SlotType::Unknown);
                }
                if (ok)
                {
                    const uint32_t window = std::max<uint32_t>(callee.ArgSlots, callee.RetSlots);
                    check.Def(a, window, SlotType::Invalid);
                    check.Def(a, callee.RetSlots, SlotType::Unknown);
                }
                break;
            }
            case ScriptOp::Enter:
                break;
            case ScriptOp::Ret:
                if (fn.RetSlots != 0)
                {
                    check.Fail("type-10", "RET in a function that declares a return value");
                }
                fallsThrough = false;
                break;
            case ScriptOp::RetV:
                if (b != fn.RetSlots)
                {
                    check.Fail("type-10", "RETV width does not match the declared return");
                }
                else
                {
                    (void)check.Use(a, b, SlotType::Unknown);
                }
                fallsThrough = false;
                break;
            }

            if (check.Error)
            {
                return check.Error;
            }

            if (op == ScriptOp::Bt || op == ScriptOp::Bf)
            {
                const int32_t offset = ScriptDecodeSBx(word);
                if (MaybeFailure f = propagate(static_cast<uint32_t>(after + offset), check.State))
                {
                    return f;
                }
            }
            if (op == ScriptOp::Jmp)
            {
                const int32_t offset = ScriptDecodeSAx(word);
                if (MaybeFailure f = propagate(static_cast<uint32_t>(after + offset), check.State))
                {
                    return f;
                }
            }
            if (fallsThrough)
            {
                if (MaybeFailure f = propagate(after, check.State))
                {
                    return f;
                }
            }
        }
        return std::nullopt;
    }
}

ScriptValidationResult ValidateScriptModule(const ScriptModule& module)
{
    ScriptValidationResult result;

    // Map each function to its owning declaration, if any. A function shared
    // by two declarations would make ENTER and prelude typing ambiguous.
    std::vector<const ScriptDeclaration*> owners(module.Functions.size(), nullptr);
    for (const ScriptDeclaration& decl : module.Declarations)
    {
        for (const ScriptCallbackDef& callback : decl.Callbacks)
        {
            if (owners[callback.FunctionIndex] != nullptr
                && owners[callback.FunctionIndex] != &decl)
            {
                result.Error = "function referenced by more than one declaration";
                result.Rule = "structural-6";
                result.FunctionIndex = callback.FunctionIndex;
                return result;
            }
            owners[callback.FunctionIndex] = &decl;
        }
    }

    // Flatten param slots once: LDP indexes the flattened slot space.
    std::vector<SlotType> paramSlotTypes;
    for (const ScriptParamDef& param : module.Params)
    {
        const SlotType type = SlotTypeFromScalar(param.Scalar);
        for (uint8_t s = 0; s < param.SlotCount; ++s)
        {
            paramSlotTypes.push_back(type);
        }
    }

    for (uint32_t fnIndex = 0; fnIndex < module.Functions.size(); ++fnIndex)
    {
        FnContext ctx;
        ctx.Module = &module;
        ctx.FunctionIndex = fnIndex;
        ctx.Fn = &module.Functions[fnIndex];
        ctx.Decl = owners[fnIndex];
        ctx.ParamSlotTotal = static_cast<uint32_t>(paramSlotTypes.size());
        ctx.ParamSlotTypes = paramSlotTypes;

        if (MaybeFailure f = StructuralPass(ctx))
        {
            result.Error = std::string(module.GetString(ctx.Fn->Name)) + ": " + f->Message;
            result.Rule = f->Rule;
            result.FunctionIndex = fnIndex;
            result.CodeOffset = f->CodeOffset;
            return result;
        }
        if (MaybeFailure f = TypeFlowPass(ctx))
        {
            result.Error = std::string(module.GetString(ctx.Fn->Name)) + ": " + f->Message;
            result.Rule = f->Rule;
            result.FunctionIndex = fnIndex;
            result.CodeOffset = f->CodeOffset;
            return result;
        }
    }

    result.Ok = true;
    return result;
}
