#pragma once

#include "script/ScriptBytecode.h"
#include "script/ScriptModule.h"
#include "script/ScriptVm.h"

#include <bit>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// Hand-assembly support: builds modules without the compiler so the VM and
// validator are testable against a frozen instruction set (spec E,
// milestone 2).

inline uint64_t BitsF(double v) { return std::bit_cast<uint64_t>(v); }
inline double FromBitsF(uint64_t bits) { return std::bit_cast<double>(bits); }
inline uint64_t BitsI(int32_t v) { return static_cast<uint64_t>(static_cast<uint32_t>(v)); }
inline int32_t FromBitsI(uint64_t bits) { return static_cast<int32_t>(bits); }
inline uint64_t BitsL(int64_t v) { return static_cast<uint64_t>(v); }

class ScriptAssembler
{
public:
    ScriptAssembler() { M.Strings.push_back(""); }

    uint32_t Str(std::string_view text)
    {
        for (uint32_t i = 0; i < M.Strings.size(); ++i)
        {
            if (M.Strings[i] == text)
            {
                return i;
            }
        }
        M.Strings.emplace_back(text);
        return static_cast<uint32_t>(M.Strings.size() - 1);
    }

    uint16_t Const(uint64_t bits)
    {
        M.Constants.push_back(bits);
        return static_cast<uint16_t>(M.Constants.size() - 1);
    }
    uint16_t ConstF(double v) { return Const(BitsF(v)); }
    uint16_t ConstL(int64_t v) { return Const(BitsL(v)); }

    uint8_t Import(std::string_view name, std::string_view signature, uint16_t minVersion = 1)
    {
        M.HostImports.push_back({Str(name), Str(signature), minVersion});
        return static_cast<uint8_t>(M.HostImports.size() - 1);
    }

    uint8_t FieldBind(std::string_view component, std::string_view path,
                      ScriptScalarKind scalar, uint8_t slots)
    {
        M.FieldBinds.push_back({Str(component), Str(path), static_cast<uint8_t>(scalar), slots});
        return static_cast<uint8_t>(M.FieldBinds.size() - 1);
    }

    uint8_t ComponentBind(std::string_view name)
    {
        M.ComponentBinds.push_back(Str(name));
        return static_cast<uint8_t>(M.ComponentBinds.size() - 1);
    }

    uint8_t TagBind(std::string_view name)
    {
        M.TagBinds.push_back(Str(name));
        return static_cast<uint8_t>(M.TagBinds.size() - 1);
    }

    void Param(std::string_view name, ScriptScalarKind scalar, std::vector<uint64_t> defaults)
    {
        ScriptParamDef param;
        param.Name = Str(name);
        param.Scalar = static_cast<uint8_t>(scalar);
        param.SlotCount = static_cast<uint8_t>(defaults.size());
        param.DefaultRaw = std::move(defaults);
        M.Params.push_back(std::move(param));
    }

    void BeginFn(std::string_view name, uint8_t frameSlots, uint8_t argSlots, uint8_t retSlots,
                 uint8_t flags = 0)
    {
        Current = ScriptFunctionDef{Str(name), static_cast<uint32_t>(M.Code.size()), 0,
                                    frameSlots, argSlots, retSlots, flags};
    }

    void Abc(ScriptOp op, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0)
    {
        M.Code.push_back(ScriptEncodeABC(op, a, b, c));
    }
    void ABx(ScriptOp op, uint8_t a, uint16_t bx)
    {
        M.Code.push_back(ScriptEncodeABx(op, a, bx));
    }
    void AsBx(ScriptOp op, uint8_t a, int16_t sbx)
    {
        M.Code.push_back(ScriptEncodeAsBx(op, a, sbx));
    }
    void SAx(ScriptOp op, int32_t sax)
    {
        M.Code.push_back(ScriptEncodeSAx(op, sax));
    }
    void Ext(ScriptOp op, uint8_t a, uint8_t b, uint32_t word2)
    {
        M.Code.push_back(ScriptEncodeABC(op, a, b, 0));
        M.Code.push_back(word2);
    }
    void Raw(uint32_t word) { M.Code.push_back(word); }

    uint32_t EndFn()
    {
        Current.WordCount = static_cast<uint32_t>(M.Code.size()) - Current.CodeOffset;
        M.Functions.push_back(Current);
        return static_cast<uint32_t>(M.Functions.size() - 1);
    }

    void Decl(ScriptDeclKind kind, std::string_view name,
              std::vector<std::string_view> states,
              std::vector<std::pair<std::string_view, uint32_t>> callbacks)
    {
        ScriptDeclaration decl;
        decl.Name = Str(name);
        decl.Kind = kind;
        for (const std::string_view state : states)
        {
            decl.States.push_back(Str(state));
        }
        for (const auto& [callbackName, fnIndex] : callbacks)
        {
            decl.Callbacks.push_back({Str(callbackName), fnIndex});
        }
        M.Declarations.push_back(std::move(decl));
    }

    ScriptModule M;

private:
    ScriptFunctionDef Current;
};

// Fake engine seam. Component storage is (entityBits, fieldBind) to a value
// vector; the indexed forms address element groups within it. Every mutation
// is recorded so determinism tests can compare write logs.
class FakeScriptHost final : public ScriptHostCallHandler
{
public:
    struct Write
    {
        uint64_t Entity;
        uint32_t FieldBind;
        uint32_t ElementIndex;
        std::vector<uint64_t> Values;

        bool operator==(const Write&) const = default;
    };

    std::map<std::pair<uint64_t, uint32_t>, std::vector<uint64_t>> Components;
    std::set<std::pair<uint64_t, uint32_t>> Tags;
    std::set<std::pair<uint64_t, uint32_t>> TagsUnder; // extra hierarchical matches
    std::vector<Write> WriteLog;
    std::vector<uint32_t> HostCallLog;
    std::function<ScriptTrapCode(uint32_t, std::span<uint64_t>, uint32_t)> OnHostCall;

    ScriptTrapCode HostCall(const ScriptModule&, uint32_t importIndex,
                            std::span<uint64_t> window, uint32_t argCount) override
    {
        HostCallLog.push_back(importIndex);
        if (OnHostCall)
        {
            return OnHostCall(importIndex, window, argCount);
        }
        return ScriptTrapCode::None;
    }

    ScriptTrapCode ComponentLoad(const ScriptModule&, uint64_t entityBits, uint32_t fieldBind,
                                 uint32_t elementIndex, std::span<uint64_t> out) override
    {
        const auto it = Components.find({entityBits, fieldBind});
        if (it == Components.end())
        {
            return ScriptTrapCode::NoComponent;
        }
        const size_t base = static_cast<size_t>(elementIndex) * out.size();
        if (base + out.size() > it->second.size())
        {
            return ScriptTrapCode::Bounds;
        }
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = it->second[base + i];
        }
        return ScriptTrapCode::None;
    }

    ScriptTrapCode ComponentStore(const ScriptModule&, uint64_t entityBits, uint32_t fieldBind,
                                  uint32_t elementIndex, std::span<const uint64_t> in) override
    {
        auto it = Components.find({entityBits, fieldBind});
        if (it == Components.end())
        {
            return ScriptTrapCode::NoComponent;
        }
        const size_t base = static_cast<size_t>(elementIndex) * in.size();
        if (base + in.size() > it->second.size())
        {
            return ScriptTrapCode::Bounds;
        }
        for (size_t i = 0; i < in.size(); ++i)
        {
            it->second[base + i] = in[i];
        }
        WriteLog.push_back({entityBits, fieldBind, elementIndex,
                            std::vector<uint64_t>(in.begin(), in.end())});
        return ScriptTrapCode::None;
    }

    ScriptTrapCode ComponentHas(const ScriptModule&, uint64_t entityBits, uint32_t componentBind,
                                bool& out) override
    {
        out = HasComponentBind.contains({entityBits, componentBind});
        return ScriptTrapCode::None;
    }

    ScriptTrapCode TagHas(const ScriptModule&, uint64_t entityBits, uint32_t tagBind,
                          bool hierarchical, bool& out) override
    {
        out = Tags.contains({entityBits, tagBind})
              || (hierarchical && TagsUnder.contains({entityBits, tagBind}));
        return ScriptTrapCode::None;
    }

    ScriptTrapCode TagAdd(const ScriptModule&, uint64_t entityBits, uint32_t tagBind) override
    {
        Tags.insert({entityBits, tagBind});
        return ScriptTrapCode::None;
    }

    ScriptTrapCode TagRemove(const ScriptModule&, uint64_t entityBits, uint32_t tagBind) override
    {
        Tags.erase({entityBits, tagBind});
        return ScriptTrapCode::None;
    }

    std::set<std::pair<uint64_t, uint32_t>> HasComponentBind;
};
