#include "script/ScriptCompiler.h"

#include "script/ScriptBytecode.h"

#include <bit>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>

namespace
{
    void Append(std::string& out, const char* format, ...)
    {
        char buffer[256];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        out += buffer;
    }

    void AppendConst(std::string& out, uint64_t bits)
    {
        // Constants are untyped raw slots: show both readings.
        const double f = std::bit_cast<double>(bits);
        if (f == f && f > -1.0e12 && f < 1.0e12 && bits != 0)
        {
            Append(out, "0x%016" PRIX64 " (%g)", bits, f);
        }
        else
        {
            Append(out, "0x%016" PRIX64, bits);
        }
    }
}

std::string DisassembleScriptModule(const ScriptModule& module)
{
    std::string out;
    for (const ScriptFunctionDef& fn : module.Functions)
    {
        Append(out, "fn %.*s frame=%u args=%u rets=%u\n",
               static_cast<int>(module.GetString(fn.Name).size()),
               module.GetString(fn.Name).data(), fn.FrameSlots, fn.ArgSlots, fn.RetSlots);
        for (uint32_t at = 0; at < fn.WordCount;)
        {
            const uint32_t word = module.Code[fn.CodeOffset + at];
            const ScriptOpInfo* info = FindScriptOp(ScriptDecodeOpRaw(word));
            if (info == nullptr)
            {
                Append(out, "  %4u: <bad opcode 0x%02X>\n", at, ScriptDecodeOpRaw(word));
                ++at;
                continue;
            }
            const uint8_t a = ScriptDecodeA(word);
            const uint8_t b = ScriptDecodeB(word);
            const uint8_t c = ScriptDecodeC(word);
            Append(out, "  %4u: %-8s", at, info->Name);
            uint32_t width = 1;
            switch (info->Op)
            {
            case ScriptOp::Ldc:
            {
                Append(out, "r%u, k%u = ", a, ScriptDecodeBx(word));
                AppendConst(out, ScriptDecodeBx(word) < module.Constants.size()
                                     ? module.Constants[ScriptDecodeBx(word)]
                                     : 0);
                break;
            }
            case ScriptOp::Ldi:
                Append(out, "r%u, %d", a, ScriptDecodeSBx(word));
                break;
            case ScriptOp::Ldp:
                Append(out, "r%u, param[%u]", a, ScriptDecodeBx(word));
                break;
            case ScriptOp::Jmp:
                Append(out, "-> %u", static_cast<uint32_t>(
                    static_cast<int64_t>(at) + 1 + ScriptDecodeSAx(word)));
                break;
            case ScriptOp::Bt:
            case ScriptOp::Bf:
                Append(out, "r%u -> %u", a, static_cast<uint32_t>(
                    static_cast<int64_t>(at) + 1 + ScriptDecodeSBx(word)));
                break;
            case ScriptOp::Enter:
                Append(out, "state %d", ScriptDecodeSAx(word));
                break;
            case ScriptOp::Cld:
            case ScriptOp::Cst:
            case ScriptOp::CstD:
            case ScriptOp::CstDlt:
            {
                const std::string_view component =
                    c < module.FieldBinds.size()
                        ? module.GetString(module.FieldBinds[c].ComponentName)
                        : std::string_view("?");
                const std::string_view path =
                    c < module.FieldBinds.size()
                        ? module.GetString(module.FieldBinds[c].FieldPath)
                        : std::string_view("?");
                Append(out, "r%u, r%u, %.*s.%.*s", a, b,
                       static_cast<int>(component.size()), component.data(),
                       static_cast<int>(path.size()), path.data());
                break;
            }
            case ScriptOp::CHas:
            {
                const std::string_view name =
                    c < module.ComponentBinds.size()
                        ? module.GetString(module.ComponentBinds[c])
                        : std::string_view("?");
                Append(out, "r%u, r%u, %.*s", a, b,
                       static_cast<int>(name.size()), name.data());
                break;
            }
            case ScriptOp::THas:
            case ScriptOp::THasU:
            {
                const std::string_view name = c < module.TagBinds.size()
                                                  ? module.GetString(module.TagBinds[c])
                                                  : std::string_view("?");
                Append(out, "r%u, r%u, tag\"%.*s\"", a, b,
                       static_cast<int>(name.size()), name.data());
                break;
            }
            case ScriptOp::TAdd:
            case ScriptOp::TRem:
            {
                const std::string_view name = b < module.TagBinds.size()
                                                  ? module.GetString(module.TagBinds[b])
                                                  : std::string_view("?");
                Append(out, "r%u, tag\"%.*s\"", a,
                       static_cast<int>(name.size()), name.data());
                break;
            }
            case ScriptOp::HCall:
            {
                const std::string_view name =
                    b < module.HostImports.size()
                        ? module.GetString(module.HostImports[b].Name)
                        : std::string_view("?");
                Append(out, "r%u, %.*s, argc=%u", a,
                       static_cast<int>(name.size()), name.data(), c);
                break;
            }
            case ScriptOp::Call:
            {
                const uint16_t callee = ScriptDecodeBx(word);
                const std::string_view name =
                    callee < module.Functions.size()
                        ? module.GetString(module.Functions[callee].Name)
                        : std::string_view("?");
                Append(out, "r%u, %.*s", a, static_cast<int>(name.size()), name.data());
                break;
            }
            case ScriptOp::CldX:
            case ScriptOp::CstX:
            {
                const uint32_t word2 = module.Code[fn.CodeOffset + at + 1];
                Append(out, "r%u, r%u, bind=%u idx=r%u", a, b,
                       ScriptDecodeExtFieldBind(word2), ScriptDecodeExtIndexReg(word2));
                width = 2;
                break;
            }
            case ScriptOp::AGetL:
            case ScriptOp::ASetL:
            {
                const uint32_t word2 = module.Code[fn.CodeOffset + at + 1];
                Append(out, "r%u, r%u, n=%u idx=r%u elt=%u", a, b,
                       ScriptDecodeExtElementCount(word2), ScriptDecodeExtIndexReg(word2),
                       ScriptDecodeExtElementSlots(word2));
                width = 2;
                break;
            }
            case ScriptOp::Ret:
                break;
            case ScriptOp::RetV:
                Append(out, "r%u, n=%u", a, b);
                break;
            case ScriptOp::MovW:
                Append(out, "r%u, r%u, n=%u", a, b, c);
                break;
            case ScriptOp::Mov:
            case ScriptOp::Not:
            case ScriptOp::NegI:
            case ScriptOp::NegF:
            case ScriptOp::I2F:
            case ScriptOp::L2F:
            case ScriptOp::F2I:
            case ScriptOp::I2L:
            case ScriptOp::L2I:
            case ScriptOp::RndF32:
            case ScriptOp::VLen3:
                Append(out, "r%u, r%u", a, b);
                break;
            case ScriptOp::Nop:
                break;
            default:
                Append(out, "r%u, r%u, r%u", a, b, c);
                break;
            }
            out += "\n";
            at += width;
        }
    }
    return out;
}
