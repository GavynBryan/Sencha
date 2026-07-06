#include "ScriptTestSupport.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    struct Runner
    {
        ScriptAssembler A;
        FakeScriptHost Host;
        ScriptVm Vm;

        ScriptInvokeResult Run(uint32_t fn, std::vector<uint64_t> args = {},
                               std::vector<uint64_t>* ret = nullptr, uint64_t budget = 100000)
        {
            ScriptInvokeOptions options;
            options.Budget = budget;
            options.Args = args;
            if (ret != nullptr)
            {
                options.Return = *ret;
            }
            return Vm.Invoke(A.M, fn, Host, options);
        }
    };

    constexpr int32_t kIntMin = std::numeric_limits<int32_t>::min();
    constexpr int32_t kIntMax = std::numeric_limits<int32_t>::max();

    // One function: op(r0 = lhs, r1 = rhs) then return r2.
    int32_t BinaryI32(ScriptOp op, int32_t lhs, int32_t rhs, ScriptTrapCode* trap = nullptr)
    {
        Runner r;
        r.A.BeginFn("f", 4, 2, 1);
        r.A.Abc(op, 2, 0, 1);
        r.A.Abc(ScriptOp::RetV, 2, 1);
        const uint32_t fn = r.A.EndFn();
        std::vector<uint64_t> ret(1);
        const ScriptInvokeResult result = r.Run(fn, {BitsI(lhs), BitsI(rhs)}, &ret);
        if (trap != nullptr)
        {
            *trap = result.Trap;
            return 0;
        }
        EXPECT_TRUE(result.Ok());
        return FromBitsI(ret[0]);
    }

    double BinaryF64(ScriptOp op, double lhs, double rhs)
    {
        Runner r;
        r.A.BeginFn("f", 4, 2, 1);
        r.A.Abc(op, 2, 0, 1);
        r.A.Abc(ScriptOp::RetV, 2, 1);
        const uint32_t fn = r.A.EndFn();
        std::vector<uint64_t> ret(1);
        const ScriptInvokeResult result = r.Run(fn, {BitsF(lhs), BitsF(rhs)}, &ret);
        EXPECT_TRUE(result.Ok());
        return FromBitsF(ret[0]);
    }

    bool CompareF64(ScriptOp op, double lhs, double rhs)
    {
        Runner r;
        r.A.BeginFn("f", 4, 2, 1);
        r.A.Abc(op, 2, 0, 1);
        r.A.Abc(ScriptOp::RetV, 2, 1);
        const uint32_t fn = r.A.EndFn();
        std::vector<uint64_t> ret(1);
        EXPECT_TRUE(r.Run(fn, {BitsF(lhs), BitsF(rhs)}, &ret).Ok());
        return ret[0] != 0;
    }
}

TEST(ScriptVm, IntegerArithmeticWraps)
{
    EXPECT_EQ(BinaryI32(ScriptOp::AddI, kIntMax, 1), kIntMin);
    EXPECT_EQ(BinaryI32(ScriptOp::SubI, kIntMin, 1), kIntMax);
    EXPECT_EQ(BinaryI32(ScriptOp::MulI, 65536, 65536), 0);
    EXPECT_EQ(BinaryI32(ScriptOp::AddI, 2, 3), 5);
}

TEST(ScriptVm, IntegerDivision)
{
    EXPECT_EQ(BinaryI32(ScriptOp::DivI, 7, 2), 3);
    EXPECT_EQ(BinaryI32(ScriptOp::DivI, -7, 2), -3);
    EXPECT_EQ(BinaryI32(ScriptOp::DivI, kIntMin, -1), kIntMin);
    EXPECT_EQ(BinaryI32(ScriptOp::ModI, 7, 2), 1);
    EXPECT_EQ(BinaryI32(ScriptOp::ModI, -7, 2), -1);
    EXPECT_EQ(BinaryI32(ScriptOp::ModI, kIntMin, -1), 0);

    ScriptTrapCode trap = ScriptTrapCode::None;
    (void)BinaryI32(ScriptOp::DivI, 1, 0, &trap);
    EXPECT_EQ(trap, ScriptTrapCode::DivZero);
    (void)BinaryI32(ScriptOp::ModI, 1, 0, &trap);
    EXPECT_EQ(trap, ScriptTrapCode::DivZero);
}

TEST(ScriptVm, FloatArithmeticFollowsIeee)
{
    EXPECT_EQ(BinaryF64(ScriptOp::AddF, 0.1, 0.2), 0.1 + 0.2);
    EXPECT_EQ(BinaryF64(ScriptOp::DivF, 1.0, 0.0), std::numeric_limits<double>::infinity());
    EXPECT_TRUE(std::isnan(BinaryF64(ScriptOp::DivF, 0.0, 0.0)));
}

TEST(ScriptVm, NanComparesFalse)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(CompareF64(ScriptOp::CEqF, nan, nan));
    EXPECT_FALSE(CompareF64(ScriptOp::CLtF, nan, 1.0));
    EXPECT_FALSE(CompareF64(ScriptOp::CLeF, 1.0, nan));
    EXPECT_TRUE(CompareF64(ScriptOp::CLtF, 1.0, 2.0));
}

TEST(ScriptVm, Conversions)
{
    Runner r;
    r.A.BeginFn("f", 8, 1, 1);
    r.A.Abc(ScriptOp::F2I, 1, 0);
    r.A.Abc(ScriptOp::RetV, 1, 1);
    const uint32_t f2i = r.A.EndFn();

    std::vector<uint64_t> ret(1);
    EXPECT_TRUE(r.Run(f2i, {BitsF(-2.9)}, &ret).Ok());
    EXPECT_EQ(FromBitsI(ret[0]), -2); // truncation toward zero

    EXPECT_EQ(r.Run(f2i, {BitsF(3.0e9)}, &ret).Trap, ScriptTrapCode::Conv);
    EXPECT_EQ(r.Run(f2i, {BitsF(std::numeric_limits<double>::quiet_NaN())}, &ret).Trap,
              ScriptTrapCode::Conv);
}

TEST(ScriptVm, RndF32MatchesFloatRounding)
{
    Runner r;
    r.A.BeginFn("f", 2, 1, 1);
    r.A.Abc(ScriptOp::RndF32, 1, 0);
    r.A.Abc(ScriptOp::RetV, 1, 1);
    const uint32_t fn = r.A.EndFn();
    std::vector<uint64_t> ret(1);
    EXPECT_TRUE(r.Run(fn, {BitsF(0.1)}, &ret).Ok());
    EXPECT_EQ(FromBitsF(ret[0]), static_cast<double>(static_cast<float>(0.1)));
}

TEST(ScriptVm, LoopSumsViaBranches)
{
    // sum = 0; for i in 0..5 { sum += i }  -> 10
    Runner r;
    r.A.BeginFn("f", 6, 0, 1);
    r.A.AsBx(ScriptOp::Ldi, 0, 0);  // sum
    r.A.AsBx(ScriptOp::Ldi, 1, 0);  // i
    r.A.AsBx(ScriptOp::Ldi, 2, 5);  // bound
    r.A.Abc(ScriptOp::CLtI, 3, 1, 2);   // 3
    r.A.AsBx(ScriptOp::Bf, 3, 4);       // 4: exit to 9
    r.A.Abc(ScriptOp::AddI, 0, 0, 1);   // 5
    r.A.AsBx(ScriptOp::Ldi, 4, 1);      // 6
    r.A.Abc(ScriptOp::AddI, 1, 1, 4);   // 7
    r.A.SAx(ScriptOp::Jmp, -6);         // 8: back to 3
    r.A.Abc(ScriptOp::RetV, 0, 1);      // 9
    const uint32_t fn = r.A.EndFn();
    std::vector<uint64_t> ret(1);
    EXPECT_TRUE(r.Run(fn, {}, &ret).Ok());
    EXPECT_EQ(FromBitsI(ret[0]), 10);
}

TEST(ScriptVm, VectorOpsUseFixedOrder)
{
    Runner r;
    r.A.BeginFn("f", 16, 6, 1);
    r.A.Abc(ScriptOp::VDot3, 6, 0, 3);
    r.A.Abc(ScriptOp::RetV, 6, 1);
    const uint32_t dot = r.A.EndFn();
    std::vector<uint64_t> ret(1);
    EXPECT_TRUE(r.Run(dot, {BitsF(1.0), BitsF(2.0), BitsF(3.0),
                            BitsF(4.0), BitsF(5.0), BitsF(6.0)}, &ret).Ok());
    EXPECT_EQ(FromBitsF(ret[0]), (1.0 * 4.0 + 2.0 * 5.0) + 3.0 * 6.0);

    Runner r2;
    r2.A.BeginFn("g", 16, 3, 1);
    r2.A.Abc(ScriptOp::VLen3, 3, 0);
    r2.A.Abc(ScriptOp::RetV, 3, 1);
    const uint32_t len = r2.A.EndFn();
    EXPECT_TRUE(r2.Run(len, {BitsF(3.0), BitsF(4.0), BitsF(0.0)}, &ret).Ok());
    EXPECT_EQ(FromBitsF(ret[0]), 5.0);
}

TEST(ScriptVm, LocalArrayIndexingAndBounds)
{
    // arr[3] at r0..r2 (one slot per element); read arr[i].
    Runner r;
    r.A.BeginFn("f", 8, 1, 1);
    r.A.AsBx(ScriptOp::Ldi, 1, 10);
    r.A.AsBx(ScriptOp::Ldi, 2, 20);
    r.A.AsBx(ScriptOp::Ldi, 3, 30);
    r.A.Ext(ScriptOp::AGetL, 4, 1, ScriptEncodeExtArray(3, 0, 1));
    r.A.Abc(ScriptOp::RetV, 4, 1);
    const uint32_t fn = r.A.EndFn();
    std::vector<uint64_t> ret(1);
    EXPECT_TRUE(r.Run(fn, {BitsI(2)}, &ret).Ok());
    EXPECT_EQ(FromBitsI(ret[0]), 30);

    EXPECT_EQ(r.Run(fn, {BitsI(3)}, &ret).Trap, ScriptTrapCode::Bounds);
    EXPECT_EQ(r.Run(fn, {BitsI(-1)}, &ret).Trap, ScriptTrapCode::Bounds);
}

TEST(ScriptVm, CallReturnsValueAndTrapsOnDepth)
{
    Runner r;
    r.A.BeginFn("double", 2, 1, 1);
    r.A.Abc(ScriptOp::AddI, 1, 0, 0);
    r.A.Abc(ScriptOp::RetV, 1, 1);
    const uint32_t doubler = r.A.EndFn();

    r.A.BeginFn("f", 8, 1, 1);
    r.A.Abc(ScriptOp::Mov, 4, 0);
    r.A.ABx(ScriptOp::Call, 4, static_cast<uint16_t>(doubler));
    r.A.Abc(ScriptOp::RetV, 4, 1);
    const uint32_t caller = r.A.EndFn();

    std::vector<uint64_t> ret(1);
    EXPECT_TRUE(r.Run(caller, {BitsI(21)}, &ret).Ok());
    EXPECT_EQ(FromBitsI(ret[0]), 42);

    // Infinite recursion trips the depth cap.
    Runner r2;
    r2.A.BeginFn("rec", 2, 0, 0);
    r2.A.ABx(ScriptOp::Call, 0, 0);
    r2.A.Abc(ScriptOp::Ret);
    const uint32_t rec = r2.A.EndFn();
    const ScriptInvokeResult result = r2.Run(rec);
    EXPECT_EQ(result.Trap, ScriptTrapCode::CallDepth);
}

TEST(ScriptVm, BudgetTrapsAtExactCount)
{
    // Straight line of 5 instructions (4 NOP + RET).
    Runner r;
    r.A.BeginFn("f", 1, 0, 0);
    for (int i = 0; i < 4; ++i)
    {
        r.A.Abc(ScriptOp::Nop);
    }
    r.A.Abc(ScriptOp::Ret);
    const uint32_t fn = r.A.EndFn();

    const ScriptInvokeResult ok = r.Run(fn, {}, nullptr, 5);
    EXPECT_TRUE(ok.Ok());
    EXPECT_EQ(ok.InstructionsExecuted, 5u);

    const ScriptInvokeResult trapped = r.Run(fn, {}, nullptr, 4);
    EXPECT_EQ(trapped.Trap, ScriptTrapCode::Budget);
    EXPECT_EQ(trapped.TrapCodeOffset, 4u);
    EXPECT_EQ(trapped.InstructionsExecuted, 4u);
}

TEST(ScriptVm, EnterRecordsLastTransition)
{
    Runner r;
    r.A.BeginFn("start", 10, 9, 0);
    r.A.SAx(ScriptOp::Enter, 0);
    r.A.SAx(ScriptOp::Enter, 2);
    r.A.Abc(ScriptOp::Ret);
    const uint32_t fn = r.A.EndFn();
    r.A.Decl(ScriptDeclKind::Ability, "Dash", {"A", "B", "C"}, {{"start", fn}});

    std::vector<uint64_t> args(9, 0);
    const ScriptInvokeResult result = r.Run(fn, args);
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.PendingState, 2);

    // No ENTER executed: no pending transition.
    Runner r2;
    r2.A.BeginFn("f", 1, 0, 0);
    r2.A.Abc(ScriptOp::Ret);
    EXPECT_EQ(r2.Run(r2.A.EndFn()).PendingState, -1);
}

TEST(ScriptVm, ComponentLoadStoreThroughHost)
{
    Runner r;
    const uint8_t bind = r.A.FieldBind("HookshotState", "target", ScriptScalarKind::Double, 3);
    r.A.BeginFn("f", 8, 1, 3);
    r.A.Abc(ScriptOp::Cld, 1, 0, bind);      // load vec3
    r.A.ABx(ScriptOp::Ldc, 4, r.A.ConstF(2.0));
    r.A.Abc(ScriptOp::VScale3, 1, 1, 4);
    r.A.Abc(ScriptOp::Cst, 0, 1, bind);      // store back
    r.A.Abc(ScriptOp::RetV, 1, 3);
    const uint32_t fn = r.A.EndFn();

    const uint64_t entity = 0x0000000700000009ull;
    r.Host.Components[{entity, bind}] = {BitsF(1.0), BitsF(2.0), BitsF(3.0)};

    std::vector<uint64_t> ret(3);
    EXPECT_TRUE(r.Run(fn, {entity}, &ret).Ok());
    EXPECT_EQ(FromBitsF(ret[2]), 6.0);
    ASSERT_EQ(r.Host.WriteLog.size(), 1u);
    EXPECT_EQ(r.Host.WriteLog[0].Entity, entity);
    EXPECT_EQ(FromBitsF(r.Host.WriteLog[0].Values[0]), 2.0);

    // Missing component traps.
    const ScriptInvokeResult missing = r.Run(fn, {0x42ull}, &ret);
    EXPECT_EQ(missing.Trap, ScriptTrapCode::NoComponent);
    EXPECT_EQ(missing.TrapFunction, fn);
    EXPECT_EQ(missing.TrapCodeOffset, 0u);
}

TEST(ScriptVm, TagOpsThroughHost)
{
    Runner r;
    const uint8_t anchor = r.A.TagBind("hookshot.anchor");
    const uint8_t burning = r.A.TagBind("damage.burning");
    r.A.BeginFn("f", 8, 1, 1);
    r.A.Abc(ScriptOp::THas, 1, 0, anchor);
    r.A.Abc(ScriptOp::TAdd, 0, burning);
    r.A.Abc(ScriptOp::THasU, 2, 0, burning);
    r.A.Abc(ScriptOp::TRem, 0, burning);
    r.A.Abc(ScriptOp::THas, 3, 0, burning);
    // r1 && r2 && !r3 == everything behaved
    r.A.Abc(ScriptOp::Not, 3, 3);
    r.A.Abc(ScriptOp::RetV, 1, 1);
    const uint32_t fn = r.A.EndFn();

    const uint64_t entity = 5;
    r.Host.Tags.insert({entity, anchor});
    std::vector<uint64_t> ret(1);
    EXPECT_TRUE(r.Run(fn, {entity}, &ret).Ok());
    EXPECT_EQ(ret[0], 1u);
    EXPECT_FALSE(r.Host.Tags.contains({entity, burning}));
}

TEST(ScriptVm, IntrinsicAndHostCallDispatch)
{
    Runner r;
    const uint8_t sqrtImport = r.A.Import("core.sqrt", "d d");
    const uint8_t hostImport = r.A.Import("movement.halt", "v");
    r.A.BeginFn("f", 8, 1, 1);
    r.A.Abc(ScriptOp::HCall, 0, sqrtImport, 1);
    r.A.Abc(ScriptOp::HCall, 4, hostImport, 0);
    r.A.Abc(ScriptOp::RetV, 0, 1);
    const uint32_t fn = r.A.EndFn();

    std::vector<uint64_t> ret(1);
    EXPECT_TRUE(r.Run(fn, {BitsF(9.0)}, &ret).Ok());
    EXPECT_EQ(FromBitsF(ret[0]), 3.0);
    // The intrinsic never reaches the handler; the host import does.
    ASSERT_EQ(r.Host.HostCallLog.size(), 1u);
    EXPECT_EQ(r.Host.HostCallLog[0], hostImport);
}

TEST(ScriptVm, HostTrapSurfacesWithLocation)
{
    Runner r;
    const uint8_t import = r.A.Import("physics.raycast", "S1 33fi");
    r.A.BeginFn("f", 24, 8, 0);
    r.A.Abc(ScriptOp::HCall, 0, import, 8);
    r.A.Abc(ScriptOp::Ret);
    const uint32_t fn = r.A.EndFn();
    r.Host.OnHostCall = [](uint32_t, std::span<uint64_t>, uint32_t) {
        return ScriptTrapCode::Arg;
    };
    const ScriptInvokeResult result = r.Run(fn, std::vector<uint64_t>(8, 0));
    EXPECT_EQ(result.Trap, ScriptTrapCode::Arg);
    EXPECT_EQ(result.TrapFunction, fn);
    EXPECT_EQ(result.TrapCodeOffset, 0u);
}

TEST(ScriptVm, ParamDefaultsLoadFlattened)
{
    Runner r;
    r.A.Param("range", ScriptScalarKind::Float, {BitsF(1800.0)});
    r.A.Param("target", ScriptScalarKind::Double, {BitsF(1.0), BitsF(2.0), BitsF(3.0)});
    r.A.BeginFn("f", 4, 0, 2);
    r.A.ABx(ScriptOp::Ldp, 0, 0); // range
    r.A.ABx(ScriptOp::Ldp, 1, 3); // target.z
    r.A.Abc(ScriptOp::RetV, 0, 2);
    const uint32_t fn = r.A.EndFn();
    std::vector<uint64_t> ret(2);
    EXPECT_TRUE(r.Run(fn, {}, &ret).Ok());
    EXPECT_EQ(FromBitsF(ret[0]), 1800.0);
    EXPECT_EQ(FromBitsF(ret[1]), 3.0);
}

TEST(ScriptVm, PreludeArgumentsArriveInOrder)
{
    Runner r;
    r.A.BeginFn("fixed", 6, 3, 2);
    r.A.Abc(ScriptOp::Mov, 4, 1); // tick
    r.A.Abc(ScriptOp::Mov, 5, 2); // dt
    r.A.Abc(ScriptOp::RetV, 4, 2);
    const uint32_t fn = r.A.EndFn();
    r.A.Decl(ScriptDeclKind::Behavior, "Bob", {}, {{"fixed", fn}});

    std::vector<uint64_t> ret(2);
    EXPECT_TRUE(r.Run(fn, {7ull, BitsL(481), BitsF(1.0 / 60.0)}, &ret).Ok());
    EXPECT_EQ(static_cast<int64_t>(ret[0]), 481);
    EXPECT_EQ(FromBitsF(ret[1]), 1.0 / 60.0);
}
