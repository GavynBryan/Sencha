#include "ScriptTestSupport.h"

#include <gtest/gtest.h>

namespace
{
    // A workload touching float math, intrinsics, branches, component
    // stores, and a host call: the shapes whose ordering could drift.
    struct Workload
    {
        ScriptAssembler A;
        uint32_t Fn = 0;
        uint8_t Bind = 0;
        uint8_t SinImport = 0;
        uint8_t HostImport = 0;

        Workload()
        {
            Bind = A.FieldBind("BobMotion", "height", ScriptScalarKind::Double, 1);
            SinImport = A.Import("core.sin", "d d");
            HostImport = A.Import("test.sample", "d");

            A.BeginFn("fixed", 16, 3, 0);
            // r3 = i32 loop counter, r4 = f64 accumulator
            A.AsBx(ScriptOp::Ldi, 3, 0);
            A.ABx(ScriptOp::Ldc, 4, A.ConstF(0.0));
            A.AsBx(ScriptOp::Ldi, 5, 8);
            // loop:
            A.Abc(ScriptOp::CLtI, 6, 3, 5);          // 3
            A.AsBx(ScriptOp::Bf, 6, 8);              // 4 -> 13
            A.Abc(ScriptOp::I2F, 7, 3);              // 5
            A.Abc(ScriptOp::MulF, 7, 7, 2);          // 6: i * dt
            A.Abc(ScriptOp::HCall, 7, SinImport, 1); // 7: sin
            A.Abc(ScriptOp::AddF, 4, 4, 7);          // 8
            A.Abc(ScriptOp::Cst, 0, 4, Bind);        // 9: store accumulator
            A.AsBx(ScriptOp::Ldi, 8, 1);             // 10
            A.Abc(ScriptOp::AddI, 3, 3, 8);          // 11
            A.SAx(ScriptOp::Jmp, -10);               // 12 -> 3
            A.Abc(ScriptOp::HCall, 9, HostImport, 0); // 13
            A.Abc(ScriptOp::AddF, 4, 4, 9);          // 14
            A.Abc(ScriptOp::Cst, 0, 4, Bind);        // 15
            A.Abc(ScriptOp::Ret);                    // 16
            Fn = A.EndFn();
            A.Decl(ScriptDeclKind::Behavior, "Bob", {}, {{"fixed", Fn}});
        }
    };

    struct RunRecord
    {
        std::vector<FakeScriptHost::Write> Writes;
        uint64_t Instructions = 0;

        bool operator==(const RunRecord&) const = default;
    };

    RunRecord RunOnce(const Workload& w, ScriptVm& vm, double hostValue)
    {
        FakeScriptHost host;
        host.Components[{7ull, w.Bind}] = {BitsF(0.0)};
        host.OnHostCall = [hostValue](uint32_t, std::span<uint64_t> window, uint32_t) {
            window[0] = BitsF(hostValue);
            return ScriptTrapCode::None;
        };

        ScriptInvokeOptions options;
        const uint64_t args[3] = {7ull, BitsL(481), BitsF(1.0 / 60.0)};
        options.Args = args;
        const ScriptInvokeResult result = vm.Invoke(w.A.M, w.Fn, host, options);
        EXPECT_TRUE(result.Ok());
        return {host.WriteLog, result.InstructionsExecuted};
    }
}

TEST(ScriptDeterminism, RepeatedRunsAreIdentical)
{
    const Workload w;
    ScriptVm vm;
    const RunRecord first = RunOnce(w, vm, 0.25);
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_EQ(RunOnce(w, vm, 0.25), first);
    }
}

TEST(ScriptDeterminism, TwoVmInstancesAgree)
{
    const Workload w;
    ScriptVm vmA;
    ScriptVm vmB;
    EXPECT_EQ(RunOnce(w, vmA, 0.25), RunOnce(w, vmB, 0.25));
}

TEST(ScriptDeterminism, RoundTrippedModuleAgrees)
{
    // The same program through write + parse must execute identically.
    const Workload w;
    const std::vector<std::byte> bytes = WriteScriptModule(w.A.M);
    const ScriptModuleParseResult parsed = ParseScriptModule(std::span<const std::byte>(bytes));
    ASSERT_TRUE(parsed.Ok) << parsed.Error;

    Workload reparsed;
    reparsed.A.M = parsed.Module;
    ScriptVm vmA;
    ScriptVm vmB;
    EXPECT_EQ(RunOnce(w, vmA, 0.25), RunOnce(reparsed, vmB, 0.25));
}

TEST(ScriptDeterminism, HarnessCatchesANondeterministicHostImport)
{
    // Negative control: a host import whose result varies between runs must
    // produce differing write logs, or the harness proves nothing.
    const Workload w;
    ScriptVm vm;
    const RunRecord a = RunOnce(w, vm, 0.25);
    const RunRecord b = RunOnce(w, vm, 0.5);
    EXPECT_NE(a, b);
    EXPECT_EQ(a.Instructions, b.Instructions); // same path, different data
}
