#include "ScriptTestSupport.h"

#include "script/ScriptBytecodeValidator.h"

#include <gtest/gtest.h>

namespace
{
    void ExpectRejected(const ScriptModule& module, std::string_view rule)
    {
        const ScriptValidationResult result = ValidateScriptModule(module);
        EXPECT_FALSE(result.Ok);
        EXPECT_EQ(std::string_view(result.Rule), rule) << result.Error;
    }

    void ExpectAccepted(const ScriptModule& module)
    {
        const ScriptValidationResult result = ValidateScriptModule(module);
        EXPECT_TRUE(result.Ok) << result.Rule << ": " << result.Error;
    }
}

TEST(ScriptValidator, AcceptsAStraightLineFunction)
{
    ScriptAssembler a;
    const uint16_t two = a.ConstF(2.0);
    a.BeginFn("f", 6, 0, 0);
    a.ABx(ScriptOp::Ldc, 0, two);
    a.Abc(ScriptOp::AddF, 1, 0, 0);
    a.AsBx(ScriptOp::Ldi, 2, 41);
    a.Abc(ScriptOp::AddI, 3, 2, 2);
    a.Abc(ScriptOp::Ret);
    a.EndFn();
    ExpectAccepted(a.M);
}

TEST(ScriptValidator, AcceptsAnAbilityCallbackWithLoopAndStates)
{
    ScriptAssembler a;
    a.BeginFn("start", 12, 9, 0);
    // r9 = 0; loop: r10 = r9 < 3; bf exit; r9 += 1; jmp loop; exit: enter 0; ret
    a.AsBx(ScriptOp::Ldi, 9, 0);
    a.AsBx(ScriptOp::Ldi, 11, 3);
    a.Abc(ScriptOp::CLtI, 10, 9, 11);           // offset 2
    a.AsBx(ScriptOp::Bf, 10, 3);                // to offset 7 (after +3)
    a.AsBx(ScriptOp::Ldi, 11, 1);
    a.Abc(ScriptOp::AddI, 9, 9, 11);
    a.SAx(ScriptOp::Jmp, -6);                   // back to offset 2... after=7, 7-6=1
    a.SAx(ScriptOp::Enter, 0);                  // offset 7
    a.Abc(ScriptOp::Ret);
    const uint32_t fn = a.EndFn();
    a.Decl(ScriptDeclKind::Ability, "Dash", {"Active"}, {{"start", fn}});
    // The loop re-loads r11 before the compare join, so slot types agree.
    ExpectAccepted(a.M);
}

TEST(ScriptValidator, RejectsUnassignedOpcode)
{
    ScriptAssembler a;
    a.BeginFn("f", 2, 0, 0);
    a.Raw(0x06); // unassigned opcode value
    a.Abc(ScriptOp::Ret);
    a.EndFn();
    ExpectRejected(a.M, "structural-1");
}

TEST(ScriptValidator, RejectsExtensionWordPastFunctionEnd)
{
    ScriptAssembler a;
    a.BeginFn("f", 8, 0, 0);
    a.Raw(ScriptEncodeABC(ScriptOp::CldX, 0, 1, 0)); // EXT with no second word
    a.EndFn();
    ExpectRejected(a.M, "structural-1");
}

TEST(ScriptValidator, RejectsBranchIntoExtensionWord)
{
    ScriptAssembler a;
    (void)a.FieldBind("C", "f", ScriptScalarKind::Double, 1);
    a.BeginFn("f", 8, 0, 0);
    a.SAx(ScriptOp::Jmp, 1);                                    // target = extension word
    a.Ext(ScriptOp::CldX, 0, 1, ScriptEncodeExtFieldBind(0, 2)); // words 1-2
    a.Abc(ScriptOp::Ret);
    a.EndFn();
    ExpectRejected(a.M, "structural-2");
}

TEST(ScriptValidator, RejectsBranchOutsideFunction)
{
    ScriptAssembler a;
    a.BeginFn("f", 2, 0, 0);
    a.SAx(ScriptOp::Jmp, 5);
    a.Abc(ScriptOp::Ret);
    a.EndFn();
    ExpectRejected(a.M, "structural-2");
}

TEST(ScriptValidator, RejectsConstantIndexOutOfRange)
{
    ScriptAssembler a;
    a.BeginFn("f", 2, 0, 0);
    a.ABx(ScriptOp::Ldc, 0, 3); // no constants exist
    a.Abc(ScriptOp::Ret);
    a.EndFn();
    ExpectRejected(a.M, "structural-3");
}

TEST(ScriptValidator, RejectsStateIndexOutOfRange)
{
    ScriptAssembler a;
    a.BeginFn("start", 10, 9, 0);
    a.SAx(ScriptOp::Enter, 1); // only one state exists
    a.Abc(ScriptOp::Ret);
    const uint32_t fn = a.EndFn();
    a.Decl(ScriptDeclKind::Ability, "Dash", {"Active"}, {{"start", fn}});
    ExpectRejected(a.M, "structural-3");
}

TEST(ScriptValidator, RejectsFallingOffTheEnd)
{
    ScriptAssembler a;
    a.BeginFn("f", 4, 0, 0);
    a.AsBx(ScriptOp::Ldi, 0, 1);
    a.Abc(ScriptOp::AddI, 1, 0, 0); // last instruction, not terminal
    a.EndFn();
    ExpectRejected(a.M, "structural-4");
}

TEST(ScriptValidator, RejectsRegisterOutOfFrame)
{
    ScriptAssembler a;
    a.BeginFn("f", 4, 0, 0);
    a.AsBx(ScriptOp::Ldi, 250, 1);
    a.Abc(ScriptOp::Ret);
    a.EndFn();
    ExpectRejected(a.M, "structural-5");
}

TEST(ScriptValidator, RejectsEnterOutsideAbilityOrBehavior)
{
    ScriptAssembler a;
    a.BeginFn("f", 2, 0, 0);
    a.SAx(ScriptOp::Enter, 0);
    a.Abc(ScriptOp::Ret);
    a.EndFn();
    ExpectRejected(a.M, "structural-6");

    ScriptAssembler b;
    b.BeginFn("on_enter", 6, 4, 0);
    b.SAx(ScriptOp::Enter, 0);
    b.Abc(ScriptOp::Ret);
    const uint32_t fn = b.EndFn();
    b.Decl(ScriptDeclKind::Trigger, "Zone", {}, {{"on_enter", fn}});
    ExpectRejected(b.M, "structural-6");
}

TEST(ScriptValidator, RejectsFunctionSharedByTwoDeclarations)
{
    ScriptAssembler a;
    a.BeginFn("fixed", 4, 3, 0);
    a.Abc(ScriptOp::Ret);
    const uint32_t fn = a.EndFn();
    a.Decl(ScriptDeclKind::Behavior, "A", {}, {{"fixed", fn}});
    a.Decl(ScriptDeclKind::Behavior, "B", {}, {{"fixed", fn}});
    ExpectRejected(a.M, "structural-6");
}

TEST(ScriptValidator, RejectsUseBeforeDefinition)
{
    ScriptAssembler a;
    a.BeginFn("f", 4, 0, 0);
    a.Abc(ScriptOp::Mov, 0, 2); // r2 never defined
    a.Abc(ScriptOp::Ret);
    a.EndFn();
    ExpectRejected(a.M, "type-7");
}

TEST(ScriptValidator, RejectsSlotDefinedOnOnlyOnePath)
{
    ScriptAssembler a;
    a.BeginFn("f", 6, 0, 0);
    a.AsBx(ScriptOp::Ldi, 0, 1);
    a.Abc(ScriptOp::CEqI, 1, 0, 0);
    a.AsBx(ScriptOp::Bf, 1, 1);      // skip the definition of r2 on one path
    a.AsBx(ScriptOp::Ldi, 2, 7);
    a.Abc(ScriptOp::Mov, 3, 2);      // join: r2 invalid on the fallthrough path
    a.Abc(ScriptOp::Ret);
    a.EndFn();
    ExpectRejected(a.M, "type-7");
}

TEST(ScriptValidator, RejectsConcreteTypeConflictAtJoin)
{
    ScriptAssembler a;
    a.BeginFn("f", 6, 0, 0);
    a.AsBx(ScriptOp::Ldi, 0, 1);   // 0
    a.Abc(ScriptOp::CEqI, 1, 0, 0); // 1
    a.AsBx(ScriptOp::Bf, 1, 2);    // 2: to 5
    a.AsBx(ScriptOp::Ldi, 2, 7);   // 3: r2 is i32
    a.SAx(ScriptOp::Jmp, 1);       // 4: to 6
    a.Abc(ScriptOp::I2F, 2, 0);    // 5: r2 is f64
    a.Abc(ScriptOp::Mov, 3, 2);    // 6: join of i32 and f64
    a.Abc(ScriptOp::Ret);          // 7
    a.EndFn();
    ExpectRejected(a.M, "type-7");
}

TEST(ScriptValidator, RejectsWrongOperandType)
{
    ScriptAssembler a;
    a.BeginFn("f", 4, 0, 0);
    a.AsBx(ScriptOp::Ldi, 0, 3);
    a.Abc(ScriptOp::AddF, 1, 0, 0); // f64 add over i32 slots
    a.Abc(ScriptOp::Ret);
    a.EndFn();
    ExpectRejected(a.M, "type-7");
}

TEST(ScriptValidator, RejectsHostCallWindowMismatch)
{
    ScriptAssembler a;
    const uint8_t import = a.Import("core.sqrt", "d d");
    a.BeginFn("f", 4, 0, 0);
    a.AsBx(ScriptOp::Ldi, 0, 1);
    a.Abc(ScriptOp::HCall, 0, import, 2); // signature takes one f64 slot
    a.Abc(ScriptOp::Ret);
    a.EndFn();
    ExpectRejected(a.M, "type-8");
}

TEST(ScriptValidator, RejectsRetInValueReturningFunction)
{
    ScriptAssembler a;
    a.BeginFn("f", 4, 0, 1);
    a.Abc(ScriptOp::Ret);
    a.EndFn();
    ExpectRejected(a.M, "type-10");
}

TEST(ScriptValidator, RejectsRetvWidthMismatch)
{
    ScriptAssembler a;
    a.BeginFn("f", 4, 0, 1);
    a.AsBx(ScriptOp::Ldi, 0, 1);
    a.AsBx(ScriptOp::Ldi, 1, 2);
    a.Abc(ScriptOp::RetV, 0, 2); // function declares one return slot
    a.EndFn();
    ExpectRejected(a.M, "type-10");
}

TEST(ScriptValidator, RejectsCallbackWithWrongPrelude)
{
    ScriptAssembler a;
    a.BeginFn("start", 12, 3, 0); // ability prelude is nine slots
    a.Abc(ScriptOp::Ret);
    const uint32_t fn = a.EndFn();
    a.Decl(ScriptDeclKind::Ability, "Dash", {}, {{"start", fn}});
    ExpectRejected(a.M, "type-7");
}

TEST(ScriptValidator, AcceptsCallbackPreludeTypes)
{
    ScriptAssembler a;
    a.BeginFn("fixed", 8, 3, 0);
    // r0 entity, r1 i64 tick, r2 f64 dt: exercise each prelude type.
    a.Abc(ScriptOp::CEqE, 3, 0, 0);
    a.Abc(ScriptOp::AddL, 4, 1, 1);
    a.Abc(ScriptOp::AddF, 5, 2, 2);
    a.Abc(ScriptOp::Ret);
    const uint32_t fn = a.EndFn();
    a.Decl(ScriptDeclKind::Behavior, "Bob", {}, {{"fixed", fn}});
    ExpectAccepted(a.M);
}
