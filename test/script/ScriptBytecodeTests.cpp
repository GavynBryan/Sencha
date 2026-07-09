#include "script/ScriptBytecode.h"

#include <gtest/gtest.h>

static_assert(kScriptOpCount == 63, "Bytecode v0 pins 63 opcodes.");

TEST(ScriptBytecode, OpTableIsSortedAndDense)
{
    uint16_t previous = 0;
    bool first = true;
    for (const ScriptOpInfo& info : kScriptOps)
    {
        const uint16_t value = static_cast<uint8_t>(info.Op);
        if (!first)
        {
            EXPECT_GT(value, previous) << info.Name;
        }
        previous = value;
        first = false;

        const ScriptOpInfo* found = FindScriptOp(static_cast<uint8_t>(info.Op));
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->Op, info.Op);
        EXPECT_STREQ(found->Name, info.Name);
    }
}

TEST(ScriptBytecode, UnassignedOpcodesAreNull)
{
    EXPECT_EQ(FindScriptOp(0x06), nullptr);
    EXPECT_EQ(FindScriptOp(0x2F), nullptr);
    EXPECT_EQ(FindScriptOp(0xFF), nullptr);
    EXPECT_FALSE(IsScriptOpAssigned(0x06));
    EXPECT_TRUE(IsScriptOpAssigned(0x00));
    EXPECT_TRUE(IsScriptOpAssigned(0x92));
}

TEST(ScriptBytecode, AbcRoundTrip)
{
    const uint32_t word = ScriptEncodeABC(ScriptOp::AddI, 7, 200, 255);
    EXPECT_EQ(ScriptDecodeOp(word), ScriptOp::AddI);
    EXPECT_EQ(ScriptDecodeA(word), 7);
    EXPECT_EQ(ScriptDecodeB(word), 200);
    EXPECT_EQ(ScriptDecodeC(word), 255);
}

TEST(ScriptBytecode, ABxRoundTrip)
{
    const uint32_t word = ScriptEncodeABx(ScriptOp::Ldc, 3, 65535);
    EXPECT_EQ(ScriptDecodeOp(word), ScriptOp::Ldc);
    EXPECT_EQ(ScriptDecodeA(word), 3);
    EXPECT_EQ(ScriptDecodeBx(word), 65535);
}

TEST(ScriptBytecode, AsBxRoundTripNegative)
{
    const uint32_t word = ScriptEncodeAsBx(ScriptOp::Bf, 9, -32768);
    EXPECT_EQ(ScriptDecodeOp(word), ScriptOp::Bf);
    EXPECT_EQ(ScriptDecodeA(word), 9);
    EXPECT_EQ(ScriptDecodeSBx(word), -32768);
    EXPECT_EQ(ScriptDecodeSBx(ScriptEncodeAsBx(ScriptOp::Bt, 0, 32767)), 32767);
    EXPECT_EQ(ScriptDecodeSBx(ScriptEncodeAsBx(ScriptOp::Bt, 0, -1)), -1);
}

TEST(ScriptBytecode, SAxRoundTrip)
{
    EXPECT_EQ(ScriptDecodeSAx(ScriptEncodeSAx(ScriptOp::Jmp, -1)), -1);
    EXPECT_EQ(ScriptDecodeSAx(ScriptEncodeSAx(ScriptOp::Jmp, 8388607)), 8388607);
    EXPECT_EQ(ScriptDecodeSAx(ScriptEncodeSAx(ScriptOp::Jmp, -8388608)), -8388608);
    EXPECT_EQ(ScriptDecodeSAx(ScriptEncodeSAx(ScriptOp::Enter, 2)), 2);
    EXPECT_EQ(ScriptDecodeOp(ScriptEncodeSAx(ScriptOp::Jmp, -5)), ScriptOp::Jmp);
}

TEST(ScriptBytecode, ExtensionWordRoundTrip)
{
    const uint32_t fieldExt = ScriptEncodeExtFieldBind(4097, 12);
    EXPECT_EQ(ScriptDecodeExtFieldBind(fieldExt), 4097);
    EXPECT_EQ(ScriptDecodeExtIndexReg(fieldExt), 12);

    const uint32_t arrayExt = ScriptEncodeExtArray(300, 5, 3);
    EXPECT_EQ(ScriptDecodeExtElementCount(arrayExt), 300);
    EXPECT_EQ(ScriptDecodeExtIndexReg(arrayExt), 5);
    EXPECT_EQ(ScriptDecodeExtElementSlots(arrayExt), 3);
}
