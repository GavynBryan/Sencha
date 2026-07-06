#pragma once

#include <array>
#include <cstdint>

//=============================================================================
// ScriptBytecode
//
// Bytecode v0 for the T gameplay language. The instruction set is frozen by
// the spec; this header transcribes it. Instructions are one or two
// little-endian 32-bit words:
//
//   word 0: bits 0-7 opcode, 8-15 A, 16-23 B, 24-31 C
//
// Formats reinterpret the A/B/C fields:
//   ABC:  three 8-bit fields
//   ABx:  A 8-bit, Bx = unsigned 16-bit in bits 16-31
//   AsBx: A 8-bit, sBx = signed 16-bit in bits 16-31
//   sAx:  signed 24-bit in bits 8-31
//   EXT:  a second word follows; its layout is per-opcode
//
// Branch offsets are in words, relative to the instruction after the branch
// (and after its extension word, if any).
//=============================================================================

enum class ScriptOp : uint8_t
{
    Nop     = 0x00,
    Mov     = 0x01,
    MovW    = 0x02,
    Ldc     = 0x03,
    Ldi     = 0x04,
    Ldp     = 0x05,

    AddI    = 0x10,
    SubI    = 0x11,
    MulI    = 0x12,
    DivI    = 0x13,
    ModI    = 0x14,
    NegI    = 0x15,
    AddL    = 0x16,
    SubL    = 0x17,
    AddF    = 0x18,
    SubF    = 0x19,
    MulF    = 0x1A,
    DivF    = 0x1B,
    NegF    = 0x1C,

    I2F     = 0x20,
    L2F     = 0x21,
    F2I     = 0x22,
    I2L     = 0x23,
    L2I     = 0x24,
    RndF32  = 0x25,

    CEqI    = 0x30,
    CLtI    = 0x31,
    CLeI    = 0x32,
    CEqL    = 0x33,
    CLtL    = 0x34,
    CLeL    = 0x35,
    CEqF    = 0x36,
    CLtF    = 0x37,
    CLeF    = 0x38,
    CEqE    = 0x39,
    CEqT    = 0x3A,
    Not     = 0x3B,

    Jmp     = 0x40,
    Bt      = 0x41,
    Bf      = 0x42,

    VAdd3   = 0x50,
    VSub3   = 0x51,
    VScale3 = 0x52,
    VDot3   = 0x53,
    VLen3   = 0x54,

    Cld     = 0x60,
    Cst     = 0x61,
    CHas    = 0x62,
    CldX    = 0x63,
    CstX    = 0x64,

    THas    = 0x70,
    THasU   = 0x71,
    TAdd    = 0x72,
    TRem    = 0x73,

    HCall   = 0x80,
    AGetL   = 0x81,
    ASetL   = 0x82,
    Call    = 0x83,

    Enter   = 0x90,
    Ret     = 0x91,
    RetV    = 0x92,
};

enum class ScriptOpFormat : uint8_t
{
    ABC,
    ABx,
    AsBx,
    SAx,
    Ext,
};

struct ScriptOpInfo
{
    ScriptOp Op;
    const char* Name;
    ScriptOpFormat Format;
};

// Transcribed from the spec's opcode table, in opcode order. The count is the
// contract the rest of the VM (validator, disassembler, tests) pins against.
inline constexpr std::array<ScriptOpInfo, 61> kScriptOps = {{
    { ScriptOp::Nop,     "NOP",     ScriptOpFormat::ABC },
    { ScriptOp::Mov,     "MOV",     ScriptOpFormat::ABC },
    { ScriptOp::MovW,    "MOVW",    ScriptOpFormat::ABC },
    { ScriptOp::Ldc,     "LDC",     ScriptOpFormat::ABx },
    { ScriptOp::Ldi,     "LDI",     ScriptOpFormat::AsBx },
    { ScriptOp::Ldp,     "LDP",     ScriptOpFormat::ABx },
    { ScriptOp::AddI,    "ADDI",    ScriptOpFormat::ABC },
    { ScriptOp::SubI,    "SUBI",    ScriptOpFormat::ABC },
    { ScriptOp::MulI,    "MULI",    ScriptOpFormat::ABC },
    { ScriptOp::DivI,    "DIVI",    ScriptOpFormat::ABC },
    { ScriptOp::ModI,    "MODI",    ScriptOpFormat::ABC },
    { ScriptOp::NegI,    "NEGI",    ScriptOpFormat::ABC },
    { ScriptOp::AddL,    "ADDL",    ScriptOpFormat::ABC },
    { ScriptOp::SubL,    "SUBL",    ScriptOpFormat::ABC },
    { ScriptOp::AddF,    "ADDF",    ScriptOpFormat::ABC },
    { ScriptOp::SubF,    "SUBF",    ScriptOpFormat::ABC },
    { ScriptOp::MulF,    "MULF",    ScriptOpFormat::ABC },
    { ScriptOp::DivF,    "DIVF",    ScriptOpFormat::ABC },
    { ScriptOp::NegF,    "NEGF",    ScriptOpFormat::ABC },
    { ScriptOp::I2F,     "I2F",     ScriptOpFormat::ABC },
    { ScriptOp::L2F,     "L2F",     ScriptOpFormat::ABC },
    { ScriptOp::F2I,     "F2I",     ScriptOpFormat::ABC },
    { ScriptOp::I2L,     "I2L",     ScriptOpFormat::ABC },
    { ScriptOp::L2I,     "L2I",     ScriptOpFormat::ABC },
    { ScriptOp::RndF32,  "RNDF32",  ScriptOpFormat::ABC },
    { ScriptOp::CEqI,    "CEQI",    ScriptOpFormat::ABC },
    { ScriptOp::CLtI,    "CLTI",    ScriptOpFormat::ABC },
    { ScriptOp::CLeI,    "CLEI",    ScriptOpFormat::ABC },
    { ScriptOp::CEqL,    "CEQL",    ScriptOpFormat::ABC },
    { ScriptOp::CLtL,    "CLTL",    ScriptOpFormat::ABC },
    { ScriptOp::CLeL,    "CLEL",    ScriptOpFormat::ABC },
    { ScriptOp::CEqF,    "CEQF",    ScriptOpFormat::ABC },
    { ScriptOp::CLtF,    "CLTF",    ScriptOpFormat::ABC },
    { ScriptOp::CLeF,    "CLEF",    ScriptOpFormat::ABC },
    { ScriptOp::CEqE,    "CEQE",    ScriptOpFormat::ABC },
    { ScriptOp::CEqT,    "CEQT",    ScriptOpFormat::ABC },
    { ScriptOp::Not,     "NOT",     ScriptOpFormat::ABC },
    { ScriptOp::Jmp,     "JMP",     ScriptOpFormat::SAx },
    { ScriptOp::Bt,      "BT",      ScriptOpFormat::AsBx },
    { ScriptOp::Bf,      "BF",      ScriptOpFormat::AsBx },
    { ScriptOp::VAdd3,   "VADD3",   ScriptOpFormat::ABC },
    { ScriptOp::VSub3,   "VSUB3",   ScriptOpFormat::ABC },
    { ScriptOp::VScale3, "VSCALE3", ScriptOpFormat::ABC },
    { ScriptOp::VDot3,   "VDOT3",   ScriptOpFormat::ABC },
    { ScriptOp::VLen3,   "VLEN3",   ScriptOpFormat::ABC },
    { ScriptOp::Cld,     "CLD",     ScriptOpFormat::ABC },
    { ScriptOp::Cst,     "CST",     ScriptOpFormat::ABC },
    { ScriptOp::CHas,    "CHAS",    ScriptOpFormat::ABC },
    { ScriptOp::CldX,    "CLDX",    ScriptOpFormat::Ext },
    { ScriptOp::CstX,    "CSTX",    ScriptOpFormat::Ext },
    { ScriptOp::THas,    "THAS",    ScriptOpFormat::ABC },
    { ScriptOp::THasU,   "THASU",   ScriptOpFormat::ABC },
    { ScriptOp::TAdd,    "TADD",    ScriptOpFormat::ABC },
    { ScriptOp::TRem,    "TREM",    ScriptOpFormat::ABC },
    { ScriptOp::HCall,   "HCALL",   ScriptOpFormat::ABC },
    { ScriptOp::AGetL,   "AGETL",   ScriptOpFormat::Ext },
    { ScriptOp::ASetL,   "ASETL",   ScriptOpFormat::Ext },
    { ScriptOp::Call,    "CALL",    ScriptOpFormat::ABx },
    { ScriptOp::Enter,   "ENTER",   ScriptOpFormat::SAx },
    { ScriptOp::Ret,     "RET",     ScriptOpFormat::ABC },
    { ScriptOp::RetV,    "RETV",    ScriptOpFormat::ABC },
}};

inline constexpr uint32_t kScriptOpCount = static_cast<uint32_t>(kScriptOps.size());
static_assert(kScriptOpCount == 61, "Bytecode v0 defines exactly 61 opcodes.");

// Encoding limits. The encoding enforces most of these (8-bit and
// 16-bit operand fields); the rest the validator checks.
inline constexpr uint32_t kScriptMaxFrameSlots      = 256;
inline constexpr uint32_t kScriptMaxCallDepth       = 32;
inline constexpr uint32_t kScriptMaxFieldBinds      = 256;
inline constexpr uint32_t kScriptMaxComponentBinds  = 256;
inline constexpr uint32_t kScriptMaxTagBinds        = 256;
inline constexpr uint32_t kScriptMaxHostImports     = 256;
inline constexpr uint32_t kScriptMaxConstants       = 65536;
inline constexpr uint32_t kScriptMaxParams          = 65536;
inline constexpr uint32_t kScriptMaxFunctions       = 65536;

// Dense info lookup: 256 entries indexed by raw opcode byte, nullptr for
// unassigned values (which the validator rejects).
namespace ScriptBytecodeDetail
{
    constexpr std::array<const ScriptOpInfo*, 256> BuildOpTable()
    {
        std::array<const ScriptOpInfo*, 256> table{};
        for (const ScriptOpInfo& info : kScriptOps)
        {
            table[static_cast<uint8_t>(info.Op)] = &info;
        }
        return table;
    }

    inline constexpr std::array<const ScriptOpInfo*, 256> kOpTable = BuildOpTable();
}

[[nodiscard]] constexpr const ScriptOpInfo* FindScriptOp(uint8_t rawOpcode)
{
    return ScriptBytecodeDetail::kOpTable[rawOpcode];
}

[[nodiscard]] constexpr bool IsScriptOpAssigned(uint8_t rawOpcode)
{
    return FindScriptOp(rawOpcode) != nullptr;
}

// ── Word encode/decode ──────────────────────────────────────────────────────

[[nodiscard]] constexpr uint32_t ScriptEncodeABC(ScriptOp op, uint8_t a, uint8_t b, uint8_t c)
{
    return static_cast<uint32_t>(op)
         | (static_cast<uint32_t>(a) << 8)
         | (static_cast<uint32_t>(b) << 16)
         | (static_cast<uint32_t>(c) << 24);
}

[[nodiscard]] constexpr uint32_t ScriptEncodeABx(ScriptOp op, uint8_t a, uint16_t bx)
{
    return static_cast<uint32_t>(op)
         | (static_cast<uint32_t>(a) << 8)
         | (static_cast<uint32_t>(bx) << 16);
}

[[nodiscard]] constexpr uint32_t ScriptEncodeAsBx(ScriptOp op, uint8_t a, int16_t sbx)
{
    return ScriptEncodeABx(op, a, static_cast<uint16_t>(sbx));
}

// sAx is stored biased into bits 8-31; the valid range is a signed 24-bit
// value, [-8388608, 8388607].
[[nodiscard]] constexpr uint32_t ScriptEncodeSAx(ScriptOp op, int32_t sax)
{
    const uint32_t raw = static_cast<uint32_t>(sax) & 0x00FFFFFFu;
    return static_cast<uint32_t>(op) | (raw << 8);
}

[[nodiscard]] constexpr uint8_t ScriptDecodeOpRaw(uint32_t word)
{
    return static_cast<uint8_t>(word & 0xFFu);
}

[[nodiscard]] constexpr ScriptOp ScriptDecodeOp(uint32_t word)
{
    return static_cast<ScriptOp>(ScriptDecodeOpRaw(word));
}

[[nodiscard]] constexpr uint8_t ScriptDecodeA(uint32_t word)
{
    return static_cast<uint8_t>((word >> 8) & 0xFFu);
}

[[nodiscard]] constexpr uint8_t ScriptDecodeB(uint32_t word)
{
    return static_cast<uint8_t>((word >> 16) & 0xFFu);
}

[[nodiscard]] constexpr uint8_t ScriptDecodeC(uint32_t word)
{
    return static_cast<uint8_t>((word >> 24) & 0xFFu);
}

[[nodiscard]] constexpr uint16_t ScriptDecodeBx(uint32_t word)
{
    return static_cast<uint16_t>((word >> 16) & 0xFFFFu);
}

[[nodiscard]] constexpr int16_t ScriptDecodeSBx(uint32_t word)
{
    return static_cast<int16_t>(ScriptDecodeBx(word));
}

[[nodiscard]] constexpr int32_t ScriptDecodeSAx(uint32_t word)
{
    const uint32_t raw = (word >> 8) & 0x00FFFFFFu;
    // Sign-extend from 24 bits.
    return static_cast<int32_t>(raw << 8) >> 8;
}

// ── Extension word layouts ─────────────────────────────────
//
// CLDX/CSTX word 2: bits 0-15 field-bind index, bits 16-23 index register.
// AGETL/ASETL word 2: bits 0-15 element count, bits 16-23 index register,
// bits 24-31 element slot size.

[[nodiscard]] constexpr uint32_t ScriptEncodeExtFieldBind(uint16_t fieldBind, uint8_t indexReg)
{
    return static_cast<uint32_t>(fieldBind) | (static_cast<uint32_t>(indexReg) << 16);
}

[[nodiscard]] constexpr uint16_t ScriptDecodeExtFieldBind(uint32_t word2)
{
    return static_cast<uint16_t>(word2 & 0xFFFFu);
}

[[nodiscard]] constexpr uint8_t ScriptDecodeExtIndexReg(uint32_t word2)
{
    return static_cast<uint8_t>((word2 >> 16) & 0xFFu);
}

[[nodiscard]] constexpr uint32_t ScriptEncodeExtArray(uint16_t elementCount, uint8_t indexReg,
                                                      uint8_t elementSlots)
{
    return static_cast<uint32_t>(elementCount)
         | (static_cast<uint32_t>(indexReg) << 16)
         | (static_cast<uint32_t>(elementSlots) << 24);
}

[[nodiscard]] constexpr uint16_t ScriptDecodeExtElementCount(uint32_t word2)
{
    return static_cast<uint16_t>(word2 & 0xFFFFu);
}

[[nodiscard]] constexpr uint8_t ScriptDecodeExtElementSlots(uint32_t word2)
{
    return static_cast<uint8_t>((word2 >> 24) & 0xFFu);
}
