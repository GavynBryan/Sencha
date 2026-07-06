#include "script/ScriptCompiler.h"

#include "ScriptLexer.h"
#include "ScriptParser.h"
#include "script/ScriptBytecode.h"
#include "core/hash/ContentHash.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <climits>
#include <map>
#include <memory>
#include <set>

//=============================================================================
// Typecheck + codegen in one pass over the merged AST. Types are checked as
// instructions are emitted, so every error fires before the offending node
// produces code. Register allocation is stack-style: named locals get fixed
// slots, expression temporaries grow above them and release per statement.
//=============================================================================

namespace
{
    //─────────────────────────────────────────────────────────────────────
    // Semantic types
    //─────────────────────────────────────────────────────────────────────

    struct SType
    {
        enum class K : uint8_t
        {
            Void, Bool, I32, I64, F32, F64, Vec2, Vec3, Quat,
            Entity, Tag, Cue, Prefab, Asset,
            EnumT,        // Index = enum index
            ComponentT,   // Index = component index (script) or host component (Index | kHostBit)
            RecordT,      // Index = host record id (1 = RayHit, 2 = SweepHit, 3 = OverlapHits)
            ArrayT,       // Elt scalar kind, Count elements
            Ctx,          // Index = declaration kind
            Namespace,    // Index = namespace id
            HostEnum,     // Index = host enum id (0 = QueryMask)
            TypeName,     // a component or type used as a value (has(), remove())
        };

        K Kind = K::Void;
        uint32_t Index = 0;
        uint32_t Count = 0;
        K Elt = K::Void;

        [[nodiscard]] bool Is(K kind) const { return Kind == kind; }
        bool operator==(const SType&) const = default;
    };

    constexpr uint32_t kHostComponentBit = 0x80000000u;

    [[nodiscard]] uint32_t Width(const SType& type)
    {
        switch (type.Kind)
        {
        case SType::K::Void: return 0;
        case SType::K::Vec2: return 2;
        case SType::K::Vec3: return 3;
        case SType::K::Quat: return 4;
        case SType::K::RecordT: return type.Index == 3 ? 17u : 9u;
        case SType::K::ArrayT: return type.Count;
        default: return 1;
        }
    }

    [[nodiscard]] bool IsNumeric(const SType& type)
    {
        return type.Is(SType::K::I32) || type.Is(SType::K::I64) || type.Is(SType::K::F32)
               || type.Is(SType::K::F64);
    }

    [[nodiscard]] bool IsFloat(const SType& type)
    {
        return type.Is(SType::K::F32) || type.Is(SType::K::F64);
    }

    enum NamespaceId : uint32_t
    {
        NsPhysics = 0,
        NsMovement = 1,
        NsCommands = 2,
        NsRandom = 3,
    };

    //─────────────────────────────────────────────────────────────────────
    // Host surface (spec section C)
    //─────────────────────────────────────────────────────────────────────

    struct HostRecordField
    {
        std::string_view Name;
        SType Type;
        uint32_t Offset;
    };

    // RayHit / SweepHit: valid, entity, position, normal, distance.
    constexpr HostRecordField kHitFields[] = {
        {"valid", {SType::K::Bool}, 0},
        {"entity", {SType::K::Entity}, 1},
        {"position", {SType::K::Vec3}, 2},
        {"normal", {SType::K::Vec3}, 5},
        {"distance", {SType::K::F32}, 8},
    };

    constexpr HostRecordField kOverlapFields[] = {
        {"count", {SType::K::I32}, 0},
        {"entities", {SType::K::ArrayT, 0, 16, SType::K::Entity}, 1},
    };

    [[nodiscard]] std::span<const HostRecordField> RecordFields(uint32_t recordId)
    {
        return recordId == 3 ? std::span<const HostRecordField>(kOverlapFields)
                             : std::span<const HostRecordField>(kHitFields);
    }

    struct HostFnDecl
    {
        NamespaceId Ns;
        std::string_view Name;
        std::string_view ImportName;
        std::string_view Signature;
        std::initializer_list<SType> Args;
        SType Return;
    };

    const HostFnDecl kHostFns[] = {
        {NsPhysics, "raycast", "physics.raycast", "S1 33fi",
         {{SType::K::Vec3}, {SType::K::Vec3}, {SType::K::F32}, {SType::K::HostEnum}},
         {SType::K::RecordT, 1}},
        {NsPhysics, "sweep_sphere", "physics.sweep_sphere", "S2 3f3fi",
         {{SType::K::Vec3}, {SType::K::F32}, {SType::K::Vec3}, {SType::K::F32}, {SType::K::HostEnum}},
         {SType::K::RecordT, 2}},
        {NsPhysics, "overlap_sphere", "physics.overlap_sphere", "S3 3fi",
         {{SType::K::Vec3}, {SType::K::F32}, {SType::K::HostEnum}},
         {SType::K::RecordT, 3}},
        {NsMovement, "pull_toward", "movement.pull_toward", "v 3ff",
         {{SType::K::Vec3}, {SType::K::F32}, {SType::K::F32}}, {SType::K::Void}},
        {NsMovement, "impulse", "movement.impulse", "v 3", {{SType::K::Vec3}}, {SType::K::Void}},
        {NsMovement, "halt", "movement.halt", "v", {}, {SType::K::Void}},
        {NsCommands, "destroy", "commands.destroy", "v e", {{SType::K::Entity}}, {SType::K::Void}},
        {NsCommands, "spawn", "commands.spawn", "v A3",
         {{SType::K::Prefab}, {SType::K::Vec3}}, {SType::K::Void}},
        {NsRandom, "f32", "random.f32", "f", {}, {SType::K::F32}},
        {NsRandom, "range", "random.range", "i ii",
         {{SType::K::I32}, {SType::K::I32}}, {SType::K::I32}},
        {NsRandom, "unit_vec3", "random.unit_vec3", "3", {}, {SType::K::Vec3}},
    };

    struct BuiltinFn
    {
        std::string_view Name;
        std::string_view FloatImport; // intrinsic import for float args ("" = opcode path)
        std::string_view IntImport;   // intrinsic import for i32 args ("" = float only)
        uint32_t Arity;
    };

    const BuiltinFn kMathBuiltins[] = {
        {"sqrt", "core.sqrt", "", 1},
        {"sin", "core.sin", "", 1},
        {"cos", "core.cos", "", 1},
        {"abs", "core.abs_f", "core.abs_i", 1},
        {"min", "core.min_f", "core.min_i", 2},
        {"max", "core.max_f", "core.max_i", 2},
        {"clamp", "core.clamp_f", "core.clamp_i", 3},
        {"lerp", "core.lerp_f", "", 3},
    };

    //─────────────────────────────────────────────────────────────────────
    // Program-wide collected declarations
    //─────────────────────────────────────────────────────────────────────

    struct ComponentLeaf
    {
        std::string Path; // dotted, e.g. "target.x"
        ScriptScalarKind Scalar = ScriptScalarKind::Float;
        uint8_t ArrayCount = 1;
        uint16_t ByteOffset = 0;
        uint64_t DefaultScalarRaw = 0;   // scalar-format bits for the schema
        uint64_t DefaultRegisterRaw = 0; // register-format bits for literals
    };

    struct ComponentFieldSem
    {
        std::string_view Name;
        SType Type;
        std::string EnginePath;          // bind path for the whole field
        ScriptScalarKind BackingScalar = ScriptScalarKind::Float;
        uint8_t SlotCount = 1;
        uint32_t FirstLeaf = 0;
        uint32_t LeafCount = 0;
    };

    struct ComponentSem
    {
        std::string_view Name;
        bool IsHost = false;
        std::vector<ComponentFieldSem> Fields;
        std::vector<ComponentLeaf> Leaves; // script components only
    };

    struct EnumSem
    {
        std::string_view Name;
        std::vector<std::string_view> Members;
    };

    struct ConstSem
    {
        SType Type;
        uint64_t RegisterBits = 0;
    };

    struct ParamSem
    {
        SType Type;
        uint32_t SlotIndex = 0;
    };

    struct FnSem
    {
        const ScriptAstFn* Ast = nullptr;
        const ScriptAstBlock* Block = nullptr; // null for free functions
        uint16_t FileIndex = 0;
        uint32_t FunctionIndex = 0;
        std::string FullName;
        std::vector<SType> ParamTypes;
        SType Return{SType::K::Void};
        uint8_t ArgSlots = 0;
        uint8_t RetSlots = 0;
    };

    //─────────────────────────────────────────────────────────────────────
    // Compilation context
    //─────────────────────────────────────────────────────────────────────

    struct CompileFailure
    {
        std::string File;
        uint32_t Line = 0;
        uint32_t Col = 0;
        std::string Message;
    };

    struct Ctx
    {
        const ScriptHostDecls* Host = nullptr;
        ScriptModule Module;
        std::optional<CompileFailure> Failure;

        std::vector<std::string> Files; // per FileIndex

        std::vector<ComponentSem> Components;
        std::vector<EnumSem> Enums;
        std::map<std::string_view, ConstSem> GlobalConsts;
        std::vector<FnSem> Fns;
        std::map<const ScriptAstBlock*, std::map<std::string_view, ConstSem>> BlockConsts;
        std::map<const ScriptAstBlock*, std::map<std::string_view, ParamSem>> BlockParams;
        uint32_t ParamSlotCursor = 0;

        // Dedup tables into the module sections.
        std::map<std::string, uint32_t> StringIndex;
        std::map<uint64_t, uint16_t> ConstIndex;
        std::map<std::pair<std::string, std::string>, uint8_t> FieldBindIndex;
        std::map<std::string, uint8_t> ComponentBindIndex;
        std::map<std::string, uint8_t> TagBindIndex;
        std::map<std::pair<std::string, uint8_t>, uint8_t> AssetBindIndex;
        std::map<std::string, uint8_t> ImportIndex;
        std::map<std::pair<std::string, std::string>, uint16_t> HostEnumConst;

        bool Fail(uint16_t file, uint32_t line, uint32_t col, std::string message)
        {
            if (!Failure)
            {
                Failure = CompileFailure{Files[file], line, col, std::move(message)};
            }
            return false;
        }

        uint32_t Str(std::string_view text)
        {
            const std::string key(text);
            auto it = StringIndex.find(key);
            if (it != StringIndex.end())
            {
                return it->second;
            }
            Module.Strings.push_back(key);
            const uint32_t index = static_cast<uint32_t>(Module.Strings.size() - 1);
            StringIndex.emplace(key, index);
            return index;
        }

        uint16_t Const(uint64_t bits)
        {
            auto it = ConstIndex.find(bits);
            if (it != ConstIndex.end())
            {
                return it->second;
            }
            Module.Constants.push_back(bits);
            const uint16_t index = static_cast<uint16_t>(Module.Constants.size() - 1);
            ConstIndex.emplace(bits, index);
            return index;
        }

        uint8_t ImportFn(std::string_view name, std::string_view signature)
        {
            const std::string key(name);
            auto it = ImportIndex.find(key);
            if (it != ImportIndex.end())
            {
                return it->second;
            }
            Module.HostImports.push_back({Str(name), Str(signature), 1});
            const uint8_t index = static_cast<uint8_t>(Module.HostImports.size() - 1);
            ImportIndex.emplace(key, index);
            return index;
        }

        int32_t FindComponent(std::string_view name) const
        {
            for (uint32_t i = 0; i < Components.size(); ++i)
            {
                if (Components[i].Name == name)
                {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        int32_t FindEnum(std::string_view name) const
        {
            for (uint32_t i = 0; i < Enums.size(); ++i)
            {
                if (Enums[i].Name == name)
                {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        FnSem* FindFn(std::string_view name, const ScriptAstBlock* block)
        {
            for (FnSem& fn : Fns)
            {
                if (fn.Block == block && fn.Ast->Name == name && fn.Ast->State.empty())
                {
                    return &fn;
                }
            }
            return nullptr;
        }
    };

    [[nodiscard]] uint64_t BitsOfF64(double v) { return std::bit_cast<uint64_t>(v); }

    //─────────────────────────────────────────────────────────────────────
    // Type resolution
    //─────────────────────────────────────────────────────────────────────

    bool ResolveTypeRef(Ctx& ctx, uint16_t file, const ScriptTypeRef& ref, SType& out,
                        bool allowComponent)
    {
        auto named = [&](std::string_view name, SType& type) -> bool {
            if (name == "bool") { type = {SType::K::Bool}; return true; }
            if (name == "i32") { type = {SType::K::I32}; return true; }
            if (name == "i64") { type = {SType::K::I64}; return true; }
            if (name == "f32") { type = {SType::K::F32}; return true; }
            if (name == "f64") { type = {SType::K::F64}; return true; }
            if (name == "Vec2") { type = {SType::K::Vec2}; return true; }
            if (name == "Vec3") { type = {SType::K::Vec3}; return true; }
            if (name == "Quat") { type = {SType::K::Quat}; return true; }
            if (name == "Entity") { type = {SType::K::Entity}; return true; }
            if (name == "TagId") { type = {SType::K::Tag}; return true; }
            if (const int32_t enumIndex = ctx.FindEnum(name); enumIndex >= 0)
            {
                type = {SType::K::EnumT, static_cast<uint32_t>(enumIndex)};
                return true;
            }
            if (allowComponent)
            {
                if (const int32_t comp = ctx.FindComponent(name); comp >= 0)
                {
                    type = {SType::K::ComponentT, static_cast<uint32_t>(comp)};
                    return true;
                }
            }
            return false;
        };

        if (ref.IsArray)
        {
            SType elt;
            if (!named(ref.Name, elt) || Width(elt) != 1 || elt.Is(SType::K::EnumT))
            {
                return ctx.Fail(file, ref.Line, ref.Col,
                                "array element must be a scalar type");
            }
            out = {SType::K::ArrayT, 0, ref.ArrayCount, elt.Kind};
            return ref.ArrayCount > 0
                   || ctx.Fail(file, ref.Line, ref.Col, "array length must be positive");
        }
        if (!named(ref.Name, out))
        {
            return ctx.Fail(file, ref.Line, ref.Col,
                            "unknown type '" + std::string(ref.Name) + "'");
        }
        return true;
    }

    // Compile-time literal evaluation for const/param/default values.
    bool EvalConstLiteral(Ctx& ctx, uint16_t file, const ScriptExpr& expr, const SType& target,
                          double& outF, int64_t& outI, bool& outB)
    {
        if (expr.Kind == ScriptExpr::K::Unary && expr.Op == ScriptTokKind::Minus)
        {
            if (!EvalConstLiteral(ctx, file, *expr.Lhs, target, outF, outI, outB))
            {
                return false;
            }
            outF = -outF;
            outI = -outI;
            return true;
        }
        if (expr.Kind == ScriptExpr::K::IntLit)
        {
            outI = std::stoll(std::string(expr.Text));
            outF = static_cast<double>(outI);
            return true;
        }
        if (expr.Kind == ScriptExpr::K::FloatLit)
        {
            outF = std::stod(std::string(expr.Text));
            outI = static_cast<int64_t>(outF);
            return true;
        }
        if (expr.Kind == ScriptExpr::K::BoolLit)
        {
            outB = expr.Text == "true";
            return true;
        }
        (void)target;
        return ctx.Fail(file, expr.Line, expr.Col,
                        "const, param, and default values must be literals");
    }

    [[nodiscard]] uint64_t RegisterBitsFor(const SType& type, double f, int64_t i, bool b)
    {
        switch (type.Kind)
        {
        case SType::K::Bool: return b ? 1u : 0u;
        case SType::K::I32: return static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(i)));
        case SType::K::I64: return static_cast<uint64_t>(i);
        case SType::K::F32: return BitsOfF64(static_cast<double>(static_cast<float>(f)));
        case SType::K::F64: return BitsOfF64(f);
        default: return 0;
        }
    }

    [[nodiscard]] uint64_t ScalarBitsFor(ScriptScalarKind kind, double f, int64_t i, bool b)
    {
        switch (kind)
        {
        case ScriptScalarKind::Bool: return b ? 1u : 0u;
        case ScriptScalarKind::Int32:
        case ScriptScalarKind::UInt32:
        case ScriptScalarKind::TagId:
            return static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(i)));
        case ScriptScalarKind::Int64: return static_cast<uint64_t>(i);
        case ScriptScalarKind::Float:
            return static_cast<uint64_t>(std::bit_cast<uint32_t>(static_cast<float>(f)));
        case ScriptScalarKind::Double: return BitsOfF64(f);
        default: return 0;
        }
    }

    //─────────────────────────────────────────────────────────────────────
    // Collection: components, enums, consts, function signatures
    //─────────────────────────────────────────────────────────────────────

    struct FieldLayout
    {
        ScriptScalarKind Scalar;
        uint8_t LeafSize;
        uint8_t Align;
        const char* const* LeafNames; // null = single unnamed leaf
        uint8_t LeafCount;
    };

    bool LayoutComponent(Ctx& ctx, uint16_t file, const ScriptAstComponent& ast, ComponentSem& out)
    {
        static const char* const kVecLeaves[] = {"x", "y", "z", "w"};
        uint16_t offset = 0;

        out.Name = ast.Name;
        for (const ScriptAstField& field : ast.Fields)
        {
            SType type;
            if (!ResolveTypeRef(ctx, file, field.Type, type, /*allowComponent*/ false))
            {
                return false;
            }

            ScriptScalarKind scalar = ScriptScalarKind::Float;
            uint8_t leafSize = 4;
            uint8_t leafCount = 1;
            bool vecLeaves = false;
            switch (type.Kind)
            {
            case SType::K::Bool: scalar = ScriptScalarKind::Bool; leafSize = 1; break;
            case SType::K::I32: scalar = ScriptScalarKind::Int32; break;
            case SType::K::I64: scalar = ScriptScalarKind::Int64; leafSize = 8; break;
            case SType::K::F32: scalar = ScriptScalarKind::Float; break;
            case SType::K::F64: scalar = ScriptScalarKind::Double; leafSize = 8; break;
            case SType::K::Tag: scalar = ScriptScalarKind::TagId; break;
            case SType::K::Entity: scalar = ScriptScalarKind::Entity; leafSize = 8; break;
            case SType::K::EnumT: scalar = ScriptScalarKind::Int32; break;
            case SType::K::Vec2: scalar = ScriptScalarKind::Float; leafCount = 2; vecLeaves = true; break;
            case SType::K::Vec3: scalar = ScriptScalarKind::Float; leafCount = 3; vecLeaves = true; break;
            case SType::K::Quat: scalar = ScriptScalarKind::Float; leafCount = 4; vecLeaves = true; break;
            case SType::K::ArrayT:
            {
                switch (type.Elt)
                {
                case SType::K::Bool: scalar = ScriptScalarKind::Bool; leafSize = 1; break;
                case SType::K::I32: scalar = ScriptScalarKind::Int32; break;
                case SType::K::I64: scalar = ScriptScalarKind::Int64; leafSize = 8; break;
                case SType::K::F32: scalar = ScriptScalarKind::Float; break;
                case SType::K::F64: scalar = ScriptScalarKind::Double; leafSize = 8; break;
                case SType::K::Tag: scalar = ScriptScalarKind::TagId; break;
                case SType::K::Entity: scalar = ScriptScalarKind::Entity; leafSize = 8; break;
                default:
                    return ctx.Fail(file, field.Line, field.Col,
                                    "unsupported array element type in a component");
                }
                break;
            }
            default:
                return ctx.Fail(file, field.Line, field.Col,
                                "unsupported component field type");
            }

            double f = 0.0;
            int64_t i = 0;
            bool b = false;
            if (field.Default != nullptr)
            {
                SType literalTarget = type;
                if (type.Is(SType::K::Vec2) || type.Is(SType::K::Vec3) || type.Is(SType::K::Quat)
                    || type.Is(SType::K::ArrayT))
                {
                    return ctx.Fail(file, field.Line, field.Col,
                                    "vector and array fields take no default in v1.0");
                }
                if (!EvalConstLiteral(ctx, file, *field.Default, literalTarget, f, i, b))
                {
                    return false;
                }
            }

            const uint8_t align = leafSize;
            offset = static_cast<uint16_t>((offset + align - 1) & ~(align - 1));

            ComponentFieldSem sem;
            sem.Name = field.Name;
            sem.Type = type;
            sem.EnginePath = std::string(field.Name);
            sem.BackingScalar = scalar;
            sem.SlotCount = type.Is(SType::K::ArrayT) ? static_cast<uint8_t>(type.Count)
                                                      : static_cast<uint8_t>(Width(type));
            sem.FirstLeaf = static_cast<uint32_t>(out.Leaves.size());

            if (type.Is(SType::K::ArrayT))
            {
                ComponentLeaf leaf;
                leaf.Path = std::string(field.Name);
                leaf.Scalar = scalar;
                leaf.ArrayCount = static_cast<uint8_t>(type.Count);
                leaf.ByteOffset = offset;
                out.Leaves.push_back(std::move(leaf));
                offset = static_cast<uint16_t>(offset + leafSize * type.Count);
                sem.LeafCount = 1;
            }
            else
            {
                for (uint8_t l = 0; l < leafCount; ++l)
                {
                    ComponentLeaf leaf;
                    leaf.Path = std::string(field.Name);
                    if (vecLeaves)
                    {
                        leaf.Path += ".";
                        leaf.Path += kVecLeaves[l];
                    }
                    leaf.Scalar = scalar;
                    leaf.ByteOffset = offset;
                    if (field.Default != nullptr)
                    {
                        leaf.DefaultScalarRaw = ScalarBitsFor(scalar, f, i, b);
                        leaf.DefaultRegisterRaw = RegisterBitsFor(type, f, i, b);
                    }
                    out.Leaves.push_back(std::move(leaf));
                    offset = static_cast<uint16_t>(offset + leafSize);
                }
                sem.LeafCount = leafCount;
            }
            out.Fields.push_back(std::move(sem));
        }
        return true;
    }

    bool CollectUnit(Ctx& ctx, const ScriptAstUnit& unit)
    {
        for (const ScriptAstEnum& decl : unit.Enums)
        {
            if (ctx.FindEnum(decl.Name) >= 0)
            {
                return ctx.Fail(unit.FileIndex, decl.Line, decl.Col,
                                "duplicate enum '" + std::string(decl.Name) + "'");
            }
            ctx.Enums.push_back({decl.Name, decl.Members});
        }
        for (const ScriptAstComponent& decl : unit.Components)
        {
            if (ctx.FindComponent(decl.Name) >= 0)
            {
                return ctx.Fail(unit.FileIndex, decl.Line, decl.Col,
                                "duplicate component '" + std::string(decl.Name) + "'");
            }
            ComponentSem sem;
            if (!LayoutComponent(ctx, unit.FileIndex, decl, sem))
            {
                return false;
            }
            ctx.Components.push_back(std::move(sem));
        }
        for (const ScriptAstConst& decl : unit.Consts)
        {
            SType type;
            if (!ResolveTypeRef(ctx, unit.FileIndex, decl.Type, type, false))
            {
                return false;
            }
            double f = 0.0;
            int64_t i = 0;
            bool b = false;
            if (!EvalConstLiteral(ctx, unit.FileIndex, *decl.Value, type, f, i, b))
            {
                return false;
            }
            ctx.GlobalConsts[decl.Name] = {type, RegisterBitsFor(type, f, i, b)};
        }
        return true;
    }

    //─────────────────────────────────────────────────────────────────────
    // Callback shapes (spec section C preludes)
    //─────────────────────────────────────────────────────────────────────

    struct CallbackShape
    {
        std::string_view CtxType;
        uint8_t ArgSlots;
    };

    [[nodiscard]] CallbackShape ShapeFor(ScriptAstBlockKind kind)
    {
        switch (kind)
        {
        case ScriptAstBlockKind::Ability: return {"AbilityContext", 9};
        case ScriptAstBlockKind::Behavior: return {"BehaviorContext", 3};
        case ScriptAstBlockKind::Trigger: return {"TriggerContext", 4};
        case ScriptAstBlockKind::Interaction: return {"InteractionContext", 4};
        }
        return {"", 0};
    }

    [[nodiscard]] bool CallbackNameValid(ScriptAstBlockKind kind, std::string_view name,
                                         bool hasState)
    {
        switch (kind)
        {
        case ScriptAstBlockKind::Ability:
            return name == "start" || name == "fixed" || name == "finish" || name == "cancel"
                   || (hasState && (name == "enter" || name == "exit"));
        case ScriptAstBlockKind::Behavior:
            return name == "spawn" || name == "despawn" || name == "fixed"
                   || (hasState && (name == "enter" || name == "exit"));
        case ScriptAstBlockKind::Trigger:
            return name == "on_enter" || name == "on_exit";
        case ScriptAstBlockKind::Interaction:
            return name == "can_interact" || name == "interact";
        }
        return false;
    }

    //─────────────────────────────────────────────────────────────────────
    // Function emitter
    //─────────────────────────────────────────────────────────────────────

    struct Local
    {
        SType Type;
        uint32_t Slot;
    };

    struct Emitter
    {
        Ctx& C;
        FnSem& Fn;
        uint32_t FnCodeStart = 0;
        uint32_t NextSlot = 0;
        uint32_t MaxSlot = 0;
        std::vector<std::map<std::string_view, Local>> Scopes;

        explicit Emitter(Ctx& ctx, FnSem& fn) : C(ctx), Fn(fn)
        {
            FnCodeStart = static_cast<uint32_t>(C.Module.Code.size());
            NextSlot = fn.ArgSlots;
            MaxSlot = NextSlot;
            Scopes.emplace_back();
        }

        bool Fail(const ScriptExpr& at, std::string message)
        {
            return C.Fail(Fn.FileIndex, at.Line, at.Col, std::move(message));
        }
        bool FailAt(uint32_t line, uint32_t col, std::string message)
        {
            return C.Fail(Fn.FileIndex, line, col, std::move(message));
        }

        // ── Slot allocation ─────────────────────────────────────────────

        [[nodiscard]] uint32_t Alloc(uint32_t width)
        {
            const uint32_t slot = NextSlot;
            NextSlot += width;
            MaxSlot = std::max(MaxSlot, NextSlot);
            return slot;
        }

        struct TempScope
        {
            Emitter& E;
            uint32_t Saved;
            explicit TempScope(Emitter& e) : E(e), Saved(e.NextSlot) {}
            ~TempScope() { E.NextSlot = Saved; }
        };

        // ── Emission ────────────────────────────────────────────────────

        void Emit(uint32_t word) { C.Module.Code.push_back(word); }
        [[nodiscard]] uint32_t Here() const
        {
            return static_cast<uint32_t>(C.Module.Code.size());
        }

        void Line(uint32_t line, uint32_t col)
        {
            C.Module.LineTable.push_back({Here(), line, static_cast<uint16_t>(col), Fn.FileIndex});
        }

        // Branch backpatching: emit with offset 0, patch later.
        [[nodiscard]] uint32_t EmitBranch(ScriptOp op, uint8_t cond = 0)
        {
            const uint32_t at = Here();
            if (op == ScriptOp::Jmp)
            {
                Emit(ScriptEncodeSAx(op, 0));
            }
            else
            {
                Emit(ScriptEncodeAsBx(op, cond, 0));
            }
            return at;
        }

        void Patch(uint32_t branchAt, uint32_t target)
        {
            const int64_t offset = static_cast<int64_t>(target) - (static_cast<int64_t>(branchAt) + 1);
            uint32_t& word = C.Module.Code[branchAt];
            if (ScriptDecodeOp(word) == ScriptOp::Jmp)
            {
                word = ScriptEncodeSAx(ScriptOp::Jmp, static_cast<int32_t>(offset));
            }
            else
            {
                word = ScriptEncodeAsBx(ScriptDecodeOp(word), ScriptDecodeA(word),
                                        static_cast<int16_t>(offset));
            }
        }

        void EmitJumpTo(uint32_t target)
        {
            const int64_t offset = static_cast<int64_t>(target) - (static_cast<int64_t>(Here()) + 1);
            Emit(ScriptEncodeSAx(ScriptOp::Jmp, static_cast<int32_t>(offset)));
        }

        // ── Values ──────────────────────────────────────────────────────

        struct Value
        {
            SType Type;
            uint32_t Slot = 0;
            bool Ok = false;
        };

        [[nodiscard]] Value Bad() { return {}; }
        [[nodiscard]] Value Make(SType type, uint32_t slot) { return {type, slot, true}; }

        Value LoadConstBits(const SType& type, uint64_t bits)
        {
            const uint32_t slot = Alloc(1);
            // Small i32 values fit LDI; everything else goes to the pool.
            // Bool deliberately excluded: the validator types LDI results as
            // i32, so bool constants load via LDC (typed Unknown).
            if (type.Is(SType::K::I32) || type.Is(SType::K::EnumT))
            {
                const int32_t v = static_cast<int32_t>(bits);
                if (v >= -32768 && v <= 32767)
                {
                    Emit(ScriptEncodeAsBx(ScriptOp::Ldi, static_cast<uint8_t>(slot),
                                          static_cast<int16_t>(v)));
                    return Make(type, slot);
                }
            }
            Emit(ScriptEncodeABx(ScriptOp::Ldc, static_cast<uint8_t>(slot), C.Const(bits)));
            return Make(type, slot);
        }

        // Implicit conversion: exact f32 -> f64 widening only (spec D.2).
        bool Coerce(Value& value, const SType& target, const ScriptExpr& at)
        {
            if (value.Type == target)
            {
                return true;
            }
            if (value.Type.Is(SType::K::F32) && target.Is(SType::K::F64))
            {
                value.Type = {SType::K::F64};
                return true;
            }
            if (value.Type.Is(SType::K::HostEnum) && target.Is(SType::K::HostEnum))
            {
                return true;
            }
            return Fail(at, "type mismatch: insert an explicit conversion");
        }

        //────────────────────────────────────────────────────────────────
        // Place resolution (assignments and component access)
        //────────────────────────────────────────────────────────────────

        struct Place
        {
            enum class K { Local, ComponentField };
            K Kind = K::Local;
            SType Type;
            uint32_t Slot = 0;      // Local: base slot (+ Offset for vec members)
            uint32_t EntitySlot = 0;
            uint8_t FieldBind = 0;
            bool Ok = false;
        };

        [[nodiscard]] const Local* FindLocal(std::string_view name) const
        {
            for (auto scope = Scopes.rbegin(); scope != Scopes.rend(); ++scope)
            {
                auto it = scope->find(name);
                if (it != scope->end())
                {
                    return &it->second;
                }
            }
            return nullptr;
        }

        [[nodiscard]] static int VecMember(const SType& type, std::string_view name)
        {
            const uint32_t width = Width(type);
            if (name == "x" && width >= 2) return 0;
            if (name == "y" && width >= 2) return 1;
            if (name == "z" && width >= 3) return 2;
            if (name == "w" && width >= 4) return 3;
            return -1;
        }

        bool MakeFieldBind(const ComponentSem& component, const ComponentFieldSem& field,
                           std::string_view leaf, const ScriptExpr& at, uint8_t& outBind,
                           SType& outType, ScriptScalarKind& outScalar)
        {
            std::string path = field.EnginePath;
            SType type = field.Type;
            if (!leaf.empty())
            {
                const int member = VecMember(field.Type, leaf);
                if (member < 0)
                {
                    return Fail(at, "no vector member '" + std::string(leaf) + "'");
                }
                path += ".";
                path += leaf;
                type = field.BackingScalar == ScriptScalarKind::Double ? SType{SType::K::F64}
                                                                       : SType{SType::K::F32};
            }
            const auto key = std::make_pair(std::string(component.Name), path);
            auto it = C.FieldBindIndex.find(key);
            if (it == C.FieldBindIndex.end())
            {
                if (C.Module.FieldBinds.size() >= kScriptMaxFieldBinds)
                {
                    return Fail(at, "field bind limit reached: split the script");
                }
                ScriptFieldBind bind;
                bind.ComponentName = C.Str(component.Name);
                bind.FieldPath = C.Str(path);
                bind.Scalar = static_cast<uint8_t>(field.BackingScalar);
                bind.SlotCount = leaf.empty() ? field.SlotCount : 1;
                C.Module.FieldBinds.push_back(bind);
                it = C.FieldBindIndex.emplace(key,
                        static_cast<uint8_t>(C.Module.FieldBinds.size() - 1)).first;
            }
            outBind = it->second;
            outType = type;
            outScalar = field.BackingScalar;
            return true;
        }

        // Syntactic test: does this member chain read a component field
        // (entity.Comp.field or entity.Comp.field.leaf)?
        [[nodiscard]] bool IsComponentAccess(const ScriptExpr& expr) const
        {
            if (expr.Kind != ScriptExpr::K::Member
                || expr.Lhs->Kind != ScriptExpr::K::Member)
            {
                return false;
            }
            if (C.FindComponent(expr.Lhs->Text) >= 0)
            {
                return true; // entity.Comp.field
            }
            const ScriptExpr& lhs = *expr.Lhs;
            return lhs.Lhs->Kind == ScriptExpr::K::Member
                   && C.FindComponent(lhs.Lhs->Text) >= 0; // entity.Comp.field.leaf
        }

        // Recognizes: local | local.member | entity.Component.field[.leaf]
        Place ResolvePlace(const ScriptExpr& expr)
        {
            Place place;
            if (expr.Kind == ScriptExpr::K::Ident)
            {
                const Local* local = FindLocal(expr.Text);
                if (local == nullptr)
                {
                    Fail(expr, "unknown variable '" + std::string(expr.Text) + "'");
                    return place;
                }
                place.Kind = Place::K::Local;
                place.Type = local->Type;
                place.Slot = local->Slot;
                place.Ok = true;
                return place;
            }
            if (expr.Kind != ScriptExpr::K::Member)
            {
                Fail(expr, "this expression is not assignable");
                return place;
            }

            // Pattern: <entityExpr>.<Component>.<field>(.<leaf>)?
            const ScriptExpr* node = &expr;
            std::string_view leaf;
            const ScriptExpr* fieldNode = node;
            if (node->Lhs->Kind == ScriptExpr::K::Member)
            {
                const ScriptExpr* maybeComponent = node->Lhs.get();
                if (maybeComponent->Lhs->Kind == ScriptExpr::K::Member
                    && C.FindComponent(maybeComponent->Lhs->Text) >= 0)
                {
                    // entity.Comp.field.leaf
                    leaf = node->Text;
                    fieldNode = maybeComponent;
                }
            }
            const ScriptExpr* componentNode = fieldNode->Lhs.get();
            if (componentNode->Kind != ScriptExpr::K::Member)
            {
                // Could be local.member (vector member of a local).
                if (componentNode->Kind == ScriptExpr::K::Ident)
                {
                    const Local* local = FindLocal(componentNode->Text);
                    if (local != nullptr)
                    {
                        const int member = VecMember(local->Type, fieldNode->Text);
                        if (member < 0)
                        {
                            Fail(expr, "no member '" + std::string(fieldNode->Text) + "'");
                            return place;
                        }
                        place.Kind = Place::K::Local;
                        place.Type = {SType::K::F64}; // vector members are f64 slots
                        place.Slot = local->Slot + static_cast<uint32_t>(member);
                        place.Ok = true;
                        return place;
                    }
                }
                Fail(expr, "this expression is not assignable");
                return place;
            }

            const int32_t componentIndex = C.FindComponent(componentNode->Text);
            if (componentIndex < 0)
            {
                Fail(expr, "unknown component '" + std::string(componentNode->Text) + "'");
                return place;
            }
            Value entity = EvalExpr(*componentNode->Lhs, nullptr);
            if (!entity.Ok)
            {
                return place;
            }
            if (!entity.Type.Is(SType::K::Entity))
            {
                Fail(expr, "component access requires an entity");
                return place;
            }
            const ComponentSem& component = C.Components[componentIndex];
            const ComponentFieldSem* field = nullptr;
            for (const ComponentFieldSem& candidate : component.Fields)
            {
                if (candidate.Name == fieldNode->Text)
                {
                    field = &candidate;
                    break;
                }
            }
            if (field == nullptr)
            {
                Fail(expr, "component '" + std::string(component.Name) + "' has no field '"
                               + std::string(fieldNode->Text) + "'");
                return place;
            }
            ScriptScalarKind scalar;
            if (!MakeFieldBind(component, *field, leaf, expr, place.FieldBind, place.Type, scalar))
            {
                return place;
            }
            place.Kind = Place::K::ComponentField;
            place.EntitySlot = entity.Slot;
            place.Ok = true;
            return place;
        }

        //────────────────────────────────────────────────────────────────
        // Expression evaluation
        //────────────────────────────────────────────────────────────────

        Value EvalExpr(const ScriptExpr& expr, const SType* hint);

        Value EvalIdent(const ScriptExpr& expr)
        {
            if (const Local* local = FindLocal(expr.Text))
            {
                return Make(local->Type, local->Slot);
            }
            if (Fn.Block != nullptr)
            {
                const auto& params = C.BlockParams[Fn.Block];
                if (auto it = params.find(expr.Text); it != params.end())
                {
                    const uint32_t slot = Alloc(1);
                    Emit(ScriptEncodeABx(ScriptOp::Ldp, static_cast<uint8_t>(slot),
                                         static_cast<uint16_t>(it->second.SlotIndex)));
                    return Make(it->second.Type, slot);
                }
                const auto& consts = C.BlockConsts[Fn.Block];
                if (auto it = consts.find(expr.Text); it != consts.end())
                {
                    return LoadConstBits(it->second.Type, it->second.RegisterBits);
                }
            }
            if (auto it = C.GlobalConsts.find(expr.Text); it != C.GlobalConsts.end())
            {
                return LoadConstBits(it->second.Type, it->second.RegisterBits);
            }
            if (expr.Text == "pi")
            {
                return LoadConstBits({SType::K::F64}, BitsOfF64(3.141592653589793238462643383279));
            }
            if (expr.Text == "tau")
            {
                return LoadConstBits({SType::K::F64}, BitsOfF64(6.283185307179586476925286766559));
            }
            if (const int32_t enumIndex = C.FindEnum(expr.Text); enumIndex >= 0)
            {
                Value value = Bad();
                value.Type = {SType::K::TypeName, static_cast<uint32_t>(enumIndex)};
                value.Type.Elt = SType::K::EnumT;
                value.Ok = true;
                return value;
            }
            if (const int32_t componentIndex = C.FindComponent(expr.Text); componentIndex >= 0)
            {
                Value value = Bad();
                value.Type = {SType::K::TypeName, static_cast<uint32_t>(componentIndex)};
                value.Type.Elt = SType::K::ComponentT;
                value.Ok = true;
                return value;
            }
            if (expr.Text == "QueryMask")
            {
                Value value = Bad();
                value.Type = {SType::K::TypeName, 0};
                value.Type.Elt = SType::K::HostEnum;
                value.Ok = true;
                return value;
            }
            Fail(expr, "unknown identifier '" + std::string(expr.Text) + "'");
            return Bad();
        }

        Value EvalMember(const ScriptExpr& expr)
        {
            // Type-name members: enums and host enums.
            if (expr.Lhs->Kind == ScriptExpr::K::Ident)
            {
                Value base = Bad();
                {
                    // Avoid emitting code for pure type names.
                    const std::string_view name = expr.Lhs->Text;
                    if (const int32_t enumIndex = C.FindEnum(name); enumIndex >= 0)
                    {
                        const EnumSem& decl = C.Enums[enumIndex];
                        for (uint32_t i = 0; i < decl.Members.size(); ++i)
                        {
                            if (decl.Members[i] == expr.Text)
                            {
                                return LoadConstBits({SType::K::EnumT, static_cast<uint32_t>(enumIndex)},
                                                     i);
                            }
                        }
                        Fail(expr, "enum '" + std::string(name) + "' has no member '"
                                       + std::string(expr.Text) + "'");
                        return Bad();
                    }
                    if (name == "QueryMask")
                    {
                        // Host enum: pool placeholder patched at link (spec
                        // section B, kind 0x0D).
                        const auto key = std::make_pair(std::string("QueryMask"),
                                                        std::string(expr.Text));
                        auto it = C.HostEnumConst.find(key);
                        if (it == C.HostEnumConst.end())
                        {
                            C.Module.Constants.push_back(0);
                            const uint16_t constIndex =
                                static_cast<uint16_t>(C.Module.Constants.size() - 1);
                            C.Module.HostEnumFixups.push_back(
                                {constIndex, C.Str("QueryMask"), C.Str(expr.Text)});
                            it = C.HostEnumConst.emplace(key, constIndex).first;
                        }
                        const uint32_t slot = Alloc(1);
                        Emit(ScriptEncodeABx(ScriptOp::Ldc, static_cast<uint8_t>(slot), it->second));
                        return Make({SType::K::HostEnum, 0}, slot);
                    }
                }
                (void)base;
            }

            Value base = EvalExpr(*expr.Lhs, nullptr);
            if (!base.Ok)
            {
                return Bad();
            }

            switch (base.Type.Kind)
            {
            case SType::K::Ctx:
                return EvalCtxMember(expr, base);
            case SType::K::Vec2:
            case SType::K::Vec3:
            case SType::K::Quat:
            {
                const int member = VecMember(base.Type, expr.Text);
                if (member < 0)
                {
                    Fail(expr, "no vector member '" + std::string(expr.Text) + "'");
                    return Bad();
                }
                return Make({SType::K::F64}, base.Slot + static_cast<uint32_t>(member));
            }
            case SType::K::RecordT:
            {
                for (const HostRecordField& field : RecordFields(base.Type.Index))
                {
                    if (field.Name == expr.Text)
                    {
                        return Make(field.Type, base.Slot + field.Offset);
                    }
                }
                Fail(expr, "no record field '" + std::string(expr.Text) + "'");
                return Bad();
            }
            case SType::K::Entity:
            {
                // entity.Component: load the whole field group lazily via
                // ResolvePlace when the next member arrives. Here the member
                // must itself be a component name.
                const int32_t componentIndex = C.FindComponent(expr.Text);
                if (componentIndex < 0)
                {
                    Fail(expr, "unknown component or entity member '" + std::string(expr.Text) + "'");
                    return Bad();
                }
                // Componentview: defer; the parent Member/EvalExpr resolves
                // entity.Comp.field through ResolvePlace. A bare
                // entity.Component (no field) is not a value.
                Fail(expr, "component access needs a field: entity."
                               + std::string(expr.Text) + ".<field>");
                return Bad();
            }
            default:
                Fail(expr, "no member '" + std::string(expr.Text) + "' on this value");
                return Bad();
            }
        }

        Value EvalCtxMember(const ScriptExpr& expr, const Value& ctxValue)
        {
            const auto kind = static_cast<ScriptAstBlockKind>(ctxValue.Type.Index);
            const std::string_view name = expr.Text;
            struct Slot
            {
                std::string_view Name;
                uint32_t Index;
                SType Type;
            };
            auto lookup = [&](std::initializer_list<Slot> slots) -> Value {
                for (const Slot& slot : slots)
                {
                    if (slot.Name == name)
                    {
                        return Make(slot.Type, slot.Index);
                    }
                }
                if (name == "physics") return Make({SType::K::Namespace, NsPhysics}, 0);
                if (name == "movement") return Make({SType::K::Namespace, NsMovement}, 0);
                if (name == "commands") return Make({SType::K::Namespace, NsCommands}, 0);
                if (name == "random") return Make({SType::K::Namespace, NsRandom}, 0);
                Fail(expr, "no context member '" + std::string(name) + "'");
                return Bad();
            };
            switch (kind)
            {
            case ScriptAstBlockKind::Ability:
                return lookup({{"owner", 0, {SType::K::Entity}},
                               {"tick", 1, {SType::K::I64}},
                               {"dt", 2, {SType::K::F32}},
                               {"aim_origin", 3, {SType::K::Vec3}},
                               {"aim_direction", 6, {SType::K::Vec3}}});
            case ScriptAstBlockKind::Behavior:
                return lookup({{"entity", 0, {SType::K::Entity}},
                               {"tick", 1, {SType::K::I64}},
                               {"dt", 2, {SType::K::F32}}});
            case ScriptAstBlockKind::Trigger:
                return lookup({{"entity", 0, {SType::K::Entity}},
                               {"other", 1, {SType::K::Entity}},
                               {"tick", 2, {SType::K::I64}},
                               {"dt", 3, {SType::K::F32}}});
            case ScriptAstBlockKind::Interaction:
                return lookup({{"instigator", 0, {SType::K::Entity}},
                               {"target", 1, {SType::K::Entity}},
                               {"tick", 2, {SType::K::I64}},
                               {"dt", 3, {SType::K::F32}}});
            }
            return Bad();
        }

        // Copies a value into a fresh contiguous window region.
        void CopyTo(uint32_t dst, const Value& value)
        {
            const uint32_t width = Width(value.Type);
            if (width == 1)
            {
                Emit(ScriptEncodeABC(ScriptOp::Mov, static_cast<uint8_t>(dst),
                                     static_cast<uint8_t>(value.Slot), 0));
            }
            else
            {
                Emit(ScriptEncodeABC(ScriptOp::MovW, static_cast<uint8_t>(dst),
                                     static_cast<uint8_t>(value.Slot), static_cast<uint8_t>(width)));
            }
        }

        Value EvalHostCall(const ScriptExpr& expr, const HostFnDecl& decl)
        {
            if (expr.Args.size() != decl.Args.size())
            {
                Fail(expr, std::string(decl.ImportName) + " expects "
                               + std::to_string(decl.Args.size()) + " arguments");
                return Bad();
            }
            std::vector<Value> args;
            for (size_t i = 0; i < expr.Args.size(); ++i)
            {
                const SType expected = decl.Args.begin()[i];
                Value value = EvalExpr(*expr.Args[i], &expected);
                if (!value.Ok || !Coerce(value, expected, *expr.Args[i]))
                {
                    return Bad();
                }
                args.push_back(value);
            }
            uint32_t argWidth = 0;
            for (const SType& type : decl.Args)
            {
                argWidth += Width(type);
            }
            const uint32_t window = Alloc(std::max(argWidth, Width(decl.Return)));
            uint32_t cursor = window;
            for (const Value& value : args)
            {
                CopyTo(cursor, value);
                cursor += Width(value.Type);
            }
            const uint8_t import = C.ImportFn(decl.ImportName, decl.Signature);
            Emit(ScriptEncodeABC(ScriptOp::HCall, static_cast<uint8_t>(window), import,
                                 static_cast<uint8_t>(argWidth)));
            return decl.Return.Is(SType::K::Void) ? Make({SType::K::Void}, window)
                                                  : Make(decl.Return, window);
        }

        Value EvalBuiltinMath(const ScriptExpr& expr, const BuiltinFn& builtin)
        {
            if (expr.Args.size() != builtin.Arity)
            {
                Fail(expr, std::string(builtin.Name) + " expects "
                               + std::to_string(builtin.Arity) + " arguments");
                return Bad();
            }
            std::vector<Value> args;
            bool anyFloat = false;
            for (const ScriptExprPtr& argExpr : expr.Args)
            {
                Value value = EvalExpr(*argExpr, nullptr);
                if (!value.Ok)
                {
                    return Bad();
                }
                if (!IsNumeric(value.Type))
                {
                    Fail(*argExpr, "numeric argument expected");
                    return Bad();
                }
                anyFloat = anyFloat || IsFloat(value.Type);
                args.push_back(value);
            }
            const bool useFloat = anyFloat || builtin.IntImport.empty();
            for (size_t i = 0; i < args.size(); ++i)
            {
                if (useFloat && !IsFloat(args[i].Type))
                {
                    Fail(*expr.Args[i], "mixed integer and float arguments: convert explicitly");
                    return Bad();
                }
                if (!useFloat && !args[i].Type.Is(SType::K::I32))
                {
                    Fail(*expr.Args[i], "i32 arguments expected");
                    return Bad();
                }
            }
            const uint32_t window = Alloc(static_cast<uint32_t>(args.size()));
            for (size_t i = 0; i < args.size(); ++i)
            {
                CopyTo(window + static_cast<uint32_t>(i), args[i]);
            }
            std::string_view import = useFloat ? builtin.FloatImport : builtin.IntImport;
            std::string signature = useFloat ? "d " : "i ";
            for (size_t i = 0; i < args.size(); ++i)
            {
                signature += useFloat ? 'd' : 'i';
            }
            const uint8_t importIndex = C.ImportFn(import, signature);
            Emit(ScriptEncodeABC(ScriptOp::HCall, static_cast<uint8_t>(window), importIndex,
                                 static_cast<uint8_t>(args.size())));
            return Make(useFloat ? SType{SType::K::F64} : SType{SType::K::I32}, window);
        }

        Value EvalConversion(const ScriptExpr& expr, std::string_view target)
        {
            if (expr.Args.size() != 1)
            {
                Fail(expr, "conversion takes one argument");
                return Bad();
            }
            Value value = EvalExpr(*expr.Args[0], nullptr);
            if (!value.Ok)
            {
                return Bad();
            }
            const uint32_t dst = Alloc(1);
            auto unary = [&](ScriptOp op) {
                Emit(ScriptEncodeABC(op, static_cast<uint8_t>(dst),
                                     static_cast<uint8_t>(value.Slot), 0));
            };
            const SType::K from = value.Type.Kind;
            if (target == "f32")
            {
                if (from == SType::K::F32) return value;
                if (from == SType::K::F64) { unary(ScriptOp::RndF32); return Make({SType::K::F32}, dst); }
                if (from == SType::K::I32)
                {
                    unary(ScriptOp::I2F);
                    Emit(ScriptEncodeABC(ScriptOp::RndF32, static_cast<uint8_t>(dst),
                                         static_cast<uint8_t>(dst), 0));
                    return Make({SType::K::F32}, dst);
                }
                if (from == SType::K::I64)
                {
                    unary(ScriptOp::L2F);
                    Emit(ScriptEncodeABC(ScriptOp::RndF32, static_cast<uint8_t>(dst),
                                         static_cast<uint8_t>(dst), 0));
                    return Make({SType::K::F32}, dst);
                }
            }
            else if (target == "f64")
            {
                if (from == SType::K::F64 || from == SType::K::F32)
                {
                    value.Type = {SType::K::F64};
                    return value;
                }
                if (from == SType::K::I32) { unary(ScriptOp::I2F); return Make({SType::K::F64}, dst); }
                if (from == SType::K::I64) { unary(ScriptOp::L2F); return Make({SType::K::F64}, dst); }
            }
            else if (target == "i32")
            {
                if (from == SType::K::I32) return value;
                if (from == SType::K::I64) { unary(ScriptOp::L2I); return Make({SType::K::I32}, dst); }
                if (from == SType::K::F32 || from == SType::K::F64)
                {
                    unary(ScriptOp::F2I);
                    return Make({SType::K::I32}, dst);
                }
                if (from == SType::K::EnumT) { value.Type = {SType::K::I32}; return value; }
            }
            else if (target == "i64")
            {
                if (from == SType::K::I64) return value;
                if (from == SType::K::I32) { unary(ScriptOp::I2L); return Make({SType::K::I64}, dst); }
            }
            Fail(expr, "unsupported conversion");
            return Bad();
        }

        Value EvalEntityMethod(const ScriptExpr& expr, const Value& entity,
                               std::string_view method)
        {
            auto tagBind = [&](const ScriptExpr& arg, uint8_t& out) -> bool {
                if (arg.Kind != ScriptExpr::K::TagLit)
                {
                    return Fail(arg, "a tag literal is required here (tag\"...\")");
                }
                const std::string key(arg.Text);
                auto it = C.TagBindIndex.find(key);
                if (it == C.TagBindIndex.end())
                {
                    if (C.Module.TagBinds.size() >= kScriptMaxTagBinds)
                    {
                        return Fail(arg, "tag bind limit reached: split the script");
                    }
                    C.Module.TagBinds.push_back(C.Str(arg.Text));
                    it = C.TagBindIndex.emplace(key,
                            static_cast<uint8_t>(C.Module.TagBinds.size() - 1)).first;
                }
                out = it->second;
                return true;
            };

            if (method == "has_tag" || method == "has_tag_under")
            {
                if (expr.Args.size() != 1)
                {
                    Fail(expr, method.data() + std::string(" expects one tag literal"));
                    return Bad();
                }
                uint8_t bind = 0;
                if (!tagBind(*expr.Args[0], bind))
                {
                    return Bad();
                }
                const uint32_t dst = Alloc(1);
                Emit(ScriptEncodeABC(method == "has_tag" ? ScriptOp::THas : ScriptOp::THasU,
                                     static_cast<uint8_t>(dst),
                                     static_cast<uint8_t>(entity.Slot), bind));
                return Make({SType::K::Bool}, dst);
            }
            if (method == "add_tag" || method == "remove_tag")
            {
                if (expr.Args.size() != 1)
                {
                    Fail(expr, method.data() + std::string(" expects one tag literal"));
                    return Bad();
                }
                uint8_t bind = 0;
                if (!tagBind(*expr.Args[0], bind))
                {
                    return Bad();
                }
                Emit(ScriptEncodeABC(method == "add_tag" ? ScriptOp::TAdd : ScriptOp::TRem,
                                     static_cast<uint8_t>(entity.Slot), bind, 0));
                return Make({SType::K::Void}, 0);
            }
            if (method == "has")
            {
                if (expr.Args.size() != 1 || expr.Args[0]->Kind != ScriptExpr::K::Ident)
                {
                    Fail(expr, "has() expects a component name");
                    return Bad();
                }
                uint8_t bind = 0;
                if (!ComponentBind(*expr.Args[0], bind))
                {
                    return Bad();
                }
                const uint32_t dst = Alloc(1);
                Emit(ScriptEncodeABC(ScriptOp::CHas, static_cast<uint8_t>(dst),
                                     static_cast<uint8_t>(entity.Slot), bind));
                return Make({SType::K::Bool}, dst);
            }
            if (method == "alive")
            {
                const uint32_t window = Alloc(1);
                CopyTo(window, entity);
                const uint8_t import = C.ImportFn("entity.alive", "b e");
                Emit(ScriptEncodeABC(ScriptOp::HCall, static_cast<uint8_t>(window), import, 1));
                return Make({SType::K::Bool}, window);
            }
            Fail(expr, "no entity method '" + std::string(method) + "'");
            return Bad();
        }

        bool ComponentBind(const ScriptExpr& nameExpr, uint8_t& out)
        {
            const int32_t componentIndex = C.FindComponent(nameExpr.Text);
            if (componentIndex < 0)
            {
                return Fail(nameExpr, "unknown component '" + std::string(nameExpr.Text) + "'");
            }
            const std::string key(nameExpr.Text);
            auto it = C.ComponentBindIndex.find(key);
            if (it == C.ComponentBindIndex.end())
            {
                C.Module.ComponentBinds.push_back(C.Str(nameExpr.Text));
                it = C.ComponentBindIndex.emplace(key,
                        static_cast<uint8_t>(C.Module.ComponentBinds.size() - 1)).first;
            }
            out = it->second;
            return true;
        }

        uint8_t AssetBind(std::string_view path, ScriptAssetKind kind)
        {
            const auto key = std::make_pair(std::string(path), static_cast<uint8_t>(kind));
            auto it = C.AssetBindIndex.find(key);
            if (it == C.AssetBindIndex.end())
            {
                C.Module.AssetBinds.push_back({C.Str(path), static_cast<uint8_t>(kind)});
                it = C.AssetBindIndex.emplace(key,
                        static_cast<uint8_t>(C.Module.AssetBinds.size() - 1)).first;
            }
            return it->second;
        }

        Value EvalCommandsAdd(const ScriptExpr& expr)
        {
            if (expr.Args.size() != 2 || expr.Args[1]->Kind != ScriptExpr::K::Record)
            {
                Fail(expr, "commands.add expects (entity, Component { ... })");
                return Bad();
            }
            Value entity = EvalExpr(*expr.Args[0], nullptr);
            if (!entity.Ok || !entity.Type.Is(SType::K::Entity))
            {
                Fail(*expr.Args[0], "commands.add expects an entity first");
                return Bad();
            }
            const ScriptExpr& record = *expr.Args[1];
            const int32_t componentIndex = C.FindComponent(record.Text);
            if (componentIndex < 0 || C.Components[componentIndex].IsHost)
            {
                Fail(record, "unknown script component '" + std::string(record.Text) + "'");
                return Bad();
            }
            const ComponentSem& component = C.Components[componentIndex];

            // Window: entity, bind index, then the image in leaf order.
            uint32_t imageWidth = 0;
            for (const ComponentFieldSem& field : component.Fields)
            {
                imageWidth += field.SlotCount;
            }
            const uint32_t window = Alloc(2 + imageWidth);
            CopyTo(window, entity);
            uint8_t bind = 0;
            {
                ScriptExpr nameExpr;
                nameExpr.Kind = ScriptExpr::K::Ident;
                nameExpr.Text = record.Text;
                nameExpr.Line = record.Line;
                nameExpr.Col = record.Col;
                if (!ComponentBind(nameExpr, bind))
                {
                    return Bad();
                }
            }
            Emit(ScriptEncodeAsBx(ScriptOp::Ldi, static_cast<uint8_t>(window + 1),
                                  static_cast<int16_t>(bind)));

            // Fields may be listed in any order; unlisted fields default.
            std::set<std::string_view> seen;
            for (const std::string_view fieldName : record.Names)
            {
                if (!seen.insert(fieldName).second)
                {
                    Fail(record, "duplicate field '" + std::string(fieldName) + "' in literal");
                    return Bad();
                }
            }
            uint32_t cursor = window + 2;
            for (const ComponentFieldSem& field : component.Fields)
            {
                const ScriptExpr* valueExpr = nullptr;
                for (size_t i = 0; i < record.Names.size(); ++i)
                {
                    if (record.Names[i] == field.Name)
                    {
                        valueExpr = record.Args[i].get();
                        break;
                    }
                }
                if (valueExpr != nullptr)
                {
                    Value value = EvalExpr(*valueExpr, &field.Type);
                    if (!value.Ok || !Coerce(value, field.Type, *valueExpr))
                    {
                        return Bad();
                    }
                    CopyTo(cursor, value);
                }
                else
                {
                    // Defaults, leaf by leaf, in register format.
                    for (uint32_t l = 0; l < field.LeafCount; ++l)
                    {
                        const ComponentLeaf& leaf = component.Leaves[field.FirstLeaf + l];
                        Value value = LoadConstBits({SType::K::I64}, leaf.DefaultRegisterRaw);
                        Emit(ScriptEncodeABC(ScriptOp::Mov,
                                             static_cast<uint8_t>(cursor + l),
                                             static_cast<uint8_t>(value.Slot), 0));
                    }
                }
                cursor += field.SlotCount;
            }
            for (const std::string_view fieldName : record.Names)
            {
                bool known = false;
                for (const ComponentFieldSem& field : component.Fields)
                {
                    known = known || field.Name == fieldName;
                }
                if (!known)
                {
                    Fail(record, "component '" + std::string(record.Text) + "' has no field '"
                                     + std::string(fieldName) + "'");
                    return Bad();
                }
            }
            const uint8_t import = C.ImportFn("commands.add", "v eC");
            Emit(ScriptEncodeABC(ScriptOp::HCall, static_cast<uint8_t>(window), import,
                                 static_cast<uint8_t>(2 + imageWidth)));
            return Make({SType::K::Void}, 0);
        }

        Value EvalCommandsRemove(const ScriptExpr& expr)
        {
            if (expr.Args.size() != 2 || expr.Args[1]->Kind != ScriptExpr::K::Ident)
            {
                Fail(expr, "commands.remove expects (entity, Component)");
                return Bad();
            }
            Value entity = EvalExpr(*expr.Args[0], nullptr);
            if (!entity.Ok || !entity.Type.Is(SType::K::Entity))
            {
                Fail(*expr.Args[0], "commands.remove expects an entity first");
                return Bad();
            }
            uint8_t bind = 0;
            if (!ComponentBind(*expr.Args[1], bind))
            {
                return Bad();
            }
            const uint32_t window = Alloc(2);
            CopyTo(window, entity);
            Emit(ScriptEncodeAsBx(ScriptOp::Ldi, static_cast<uint8_t>(window + 1),
                                  static_cast<int16_t>(bind)));
            const uint8_t import = C.ImportFn("commands.remove", "v eC");
            Emit(ScriptEncodeABC(ScriptOp::HCall, static_cast<uint8_t>(window), import, 2));
            return Make({SType::K::Void}, 0);
        }

        Value EvalCtxMethod(const ScriptExpr& expr, std::string_view method)
        {
            const bool isAbility = Fn.Block != nullptr
                                   && Fn.Block->Kind == ScriptAstBlockKind::Ability;
            if (method == "cancel" || method == "finish")
            {
                if (!isAbility)
                {
                    Fail(expr, method.data() + std::string("() is ability-only"));
                    return Bad();
                }
                const uint8_t import = C.ImportFn(
                    method == "cancel" ? "ability.cancel" : "ability.finish", "v");
                Emit(ScriptEncodeABC(ScriptOp::HCall, 0, import, 0));
                return Make({SType::K::Void}, 0);
            }
            if (method == "cue" || method == "cue_at")
            {
                const size_t expected = method == "cue" ? 1u : 2u;
                if (expr.Args.size() != expected
                    || expr.Args[0]->Kind != ScriptExpr::K::CueLit)
                {
                    Fail(expr, "cue expects a cue literal (cue\"...\")");
                    return Bad();
                }
                const uint8_t bind = AssetBind(expr.Args[0]->Text, ScriptAssetKind::Cue);
                if (method == "cue")
                {
                    const uint32_t window = Alloc(1);
                    Emit(ScriptEncodeAsBx(ScriptOp::Ldi, static_cast<uint8_t>(window),
                                          static_cast<int16_t>(bind)));
                    const uint8_t import = C.ImportFn("cue.fire", "v A");
                    Emit(ScriptEncodeABC(ScriptOp::HCall, static_cast<uint8_t>(window), import, 1));
                }
                else
                {
                    Value position = EvalExpr(*expr.Args[1], nullptr);
                    if (!position.Ok || !position.Type.Is(SType::K::Vec3))
                    {
                        Fail(*expr.Args[1], "cue_at expects a Vec3 position");
                        return Bad();
                    }
                    const uint32_t window = Alloc(4);
                    Emit(ScriptEncodeAsBx(ScriptOp::Ldi, static_cast<uint8_t>(window),
                                          static_cast<int16_t>(bind)));
                    CopyTo(window + 1, position);
                    const uint8_t import = C.ImportFn("cue.fire_at", "v A3");
                    Emit(ScriptEncodeABC(ScriptOp::HCall, static_cast<uint8_t>(window), import, 4));
                }
                return Make({SType::K::Void}, 0);
            }
            Fail(expr, "no context method '" + std::string(method) + "'");
            return Bad();
        }

        Value EvalCall(const ScriptExpr& expr)
        {
            const ScriptExpr& callee = *expr.Lhs;
            if (callee.Kind == ScriptExpr::K::Ident)
            {
                const std::string_view name = callee.Text;
                if (name == "i32" || name == "i64" || name == "f32" || name == "f64")
                {
                    return EvalConversion(expr, name);
                }
                for (const BuiltinFn& builtin : kMathBuiltins)
                {
                    if (builtin.Name == name)
                    {
                        return EvalBuiltinMath(expr, builtin);
                    }
                }
                if (name == "distance" || name == "dot" || name == "cross"
                    || name == "normalize" || name == "length")
                {
                    return EvalVecBuiltin(expr, name);
                }
                if (FnSem* callee2 = C.FindFn(name, nullptr))
                {
                    return EvalUserCall(expr, *callee2);
                }
                if (Fn.Block != nullptr)
                {
                    if (FnSem* callee3 = C.FindFn(name, Fn.Block))
                    {
                        return EvalUserCall(expr, *callee3);
                    }
                }
                Fail(expr, "unknown function '" + std::string(name) + "'");
                return Bad();
            }
            if (callee.Kind == ScriptExpr::K::Member)
            {
                // Namespace, ctx, or entity methods.
                Value base = EvalExpr(*callee.Lhs, nullptr);
                if (!base.Ok)
                {
                    return Bad();
                }
                if (base.Type.Is(SType::K::Namespace))
                {
                    if (base.Type.Index == NsCommands)
                    {
                        if (callee.Text == "add") return EvalCommandsAdd(expr);
                        if (callee.Text == "remove") return EvalCommandsRemove(expr);
                    }
                    for (const HostFnDecl& decl : kHostFns)
                    {
                        if (decl.Ns == static_cast<NamespaceId>(base.Type.Index)
                            && decl.Name == callee.Text)
                        {
                            return EvalHostCall(expr, decl);
                        }
                    }
                    Fail(expr, "unknown host function '" + std::string(callee.Text) + "'");
                    return Bad();
                }
                if (base.Type.Is(SType::K::Ctx))
                {
                    return EvalCtxMethod(expr, callee.Text);
                }
                if (base.Type.Is(SType::K::Entity))
                {
                    return EvalEntityMethod(expr, base, callee.Text);
                }
                Fail(expr, "this value has no methods");
                return Bad();
            }
            Fail(expr, "this expression is not callable");
            return Bad();
        }

        Value EvalVecBuiltin(const ScriptExpr& expr, std::string_view name)
        {
            auto vecArg = [&](size_t index, Value& out) -> bool {
                out = EvalExpr(*expr.Args[index], nullptr);
                if (!out.Ok || !out.Type.Is(SType::K::Vec3))
                {
                    return Fail(*expr.Args[index], "Vec3 argument expected");
                }
                return true;
            };
            if (name == "distance")
            {
                if (expr.Args.size() != 2)
                {
                    Fail(expr, "distance expects two Vec3 arguments");
                    return Bad();
                }
                Value a, b;
                if (!vecArg(0, a) || !vecArg(1, b))
                {
                    return Bad();
                }
                const uint32_t window = Alloc(6);
                CopyTo(window, a);
                CopyTo(window + 3, b);
                const uint8_t import = C.ImportFn("core.vec3_distance", "d 33");
                Emit(ScriptEncodeABC(ScriptOp::HCall, static_cast<uint8_t>(window), import, 6));
                return Make({SType::K::F64}, window);
            }
            if (name == "dot" || name == "length")
            {
                Value a, b;
                if (name == "dot")
                {
                    if (expr.Args.size() != 2 || !vecArg(0, a) || !vecArg(1, b))
                    {
                        Fail(expr, "dot expects two Vec3 arguments");
                        return Bad();
                    }
                    const uint32_t dst = Alloc(1);
                    Emit(ScriptEncodeABC(ScriptOp::VDot3, static_cast<uint8_t>(dst),
                                         static_cast<uint8_t>(a.Slot),
                                         static_cast<uint8_t>(b.Slot)));
                    return Make({SType::K::F64}, dst);
                }
                if (expr.Args.size() != 1 || !vecArg(0, a))
                {
                    Fail(expr, "length expects one Vec3 argument");
                    return Bad();
                }
                const uint32_t dst = Alloc(1);
                Emit(ScriptEncodeABC(ScriptOp::VLen3, static_cast<uint8_t>(dst),
                                     static_cast<uint8_t>(a.Slot), 0));
                return Make({SType::K::F64}, dst);
            }
            if (name == "cross" || name == "normalize")
            {
                const bool isCross = name == "cross";
                Value a, b;
                if (isCross ? (expr.Args.size() != 2 || !vecArg(0, a) || !vecArg(1, b))
                            : (expr.Args.size() != 1 || !vecArg(0, a)))
                {
                    Fail(expr, "wrong arguments");
                    return Bad();
                }
                const uint32_t window = Alloc(isCross ? 6u : 3u);
                CopyTo(window, a);
                if (isCross)
                {
                    CopyTo(window + 3, b);
                }
                const uint8_t import = isCross ? C.ImportFn("core.vec3_cross", "3 33")
                                               : C.ImportFn("core.vec3_normalize", "3 3");
                Emit(ScriptEncodeABC(ScriptOp::HCall, static_cast<uint8_t>(window), import,
                                     isCross ? 6 : 3));
                return Make({SType::K::Vec3}, window);
            }
            Fail(expr, "unknown builtin");
            return Bad();
        }

        Value EvalUserCall(const ScriptExpr& expr, FnSem& callee)
        {
            if (expr.Args.size() != callee.ParamTypes.size())
            {
                Fail(expr, callee.FullName + " expects "
                               + std::to_string(callee.ParamTypes.size()) + " arguments");
                return Bad();
            }
            std::vector<Value> args;
            for (size_t i = 0; i < expr.Args.size(); ++i)
            {
                Value value = EvalExpr(*expr.Args[i], &callee.ParamTypes[i]);
                if (!value.Ok || !Coerce(value, callee.ParamTypes[i], *expr.Args[i]))
                {
                    return Bad();
                }
                args.push_back(value);
            }
            const uint32_t window = Alloc(std::max<uint32_t>(callee.ArgSlots, callee.RetSlots));
            uint32_t cursor = window;
            for (const Value& value : args)
            {
                CopyTo(cursor, value);
                cursor += Width(value.Type);
            }
            Emit(ScriptEncodeABx(ScriptOp::Call, static_cast<uint8_t>(window),
                                 static_cast<uint16_t>(callee.FunctionIndex)));
            return callee.Return.Is(SType::K::Void) ? Make({SType::K::Void}, window)
                                                    : Make(callee.Return, window);
        }

        Value EvalBinary(const ScriptExpr& expr)
        {
            // Short-circuit logic first.
            if (expr.Op == ScriptTokKind::AndAnd || expr.Op == ScriptTokKind::OrOr)
            {
                Value lhs = EvalExpr(*expr.Lhs, nullptr);
                if (!lhs.Ok || !lhs.Type.Is(SType::K::Bool))
                {
                    Fail(*expr.Lhs, "bool operand expected");
                    return Bad();
                }
                const uint32_t dst = Alloc(1);
                CopyTo(dst, lhs);
                const uint32_t skip = EmitBranch(
                    expr.Op == ScriptTokKind::AndAnd ? ScriptOp::Bf : ScriptOp::Bt,
                    static_cast<uint8_t>(dst));
                Value rhs = EvalExpr(*expr.Rhs, nullptr);
                if (!rhs.Ok || !rhs.Type.Is(SType::K::Bool))
                {
                    Fail(*expr.Rhs, "bool operand expected");
                    return Bad();
                }
                Emit(ScriptEncodeABC(ScriptOp::Mov, static_cast<uint8_t>(dst),
                                     static_cast<uint8_t>(rhs.Slot), 0));
                Patch(skip, Here());
                return Make({SType::K::Bool}, dst);
            }

            Value lhs = EvalExpr(*expr.Lhs, nullptr);
            if (!lhs.Ok)
            {
                return Bad();
            }
            Value rhs = EvalExpr(*expr.Rhs, nullptr);
            if (!rhs.Ok)
            {
                return Bad();
            }

            // Vector arithmetic.
            if (lhs.Type.Is(SType::K::Vec3) || rhs.Type.Is(SType::K::Vec3))
            {
                return EvalVecBinary(expr, lhs, rhs);
            }

            // Numeric unification: f32 widens to f64; everything else exact.
            if (IsFloat(lhs.Type) && IsFloat(rhs.Type) && !(lhs.Type == rhs.Type))
            {
                lhs.Type = {SType::K::F64};
                rhs.Type = {SType::K::F64};
            }

            const bool comparison = expr.Op == ScriptTokKind::EqEq
                                    || expr.Op == ScriptTokKind::NotEq
                                    || expr.Op == ScriptTokKind::Lt
                                    || expr.Op == ScriptTokKind::Le
                                    || expr.Op == ScriptTokKind::Gt
                                    || expr.Op == ScriptTokKind::Ge;

            if (!(lhs.Type == rhs.Type))
            {
                Fail(expr, "operand types differ: insert an explicit conversion");
                return Bad();
            }

            if (comparison)
            {
                return EvalComparison(expr, lhs, rhs);
            }

            ScriptOp op = ScriptOp::Nop;
            SType result = lhs.Type;
            switch (lhs.Type.Kind)
            {
            case SType::K::I32:
                switch (expr.Op)
                {
                case ScriptTokKind::Plus: op = ScriptOp::AddI; break;
                case ScriptTokKind::Minus: op = ScriptOp::SubI; break;
                case ScriptTokKind::Star: op = ScriptOp::MulI; break;
                case ScriptTokKind::Slash: op = ScriptOp::DivI; break;
                case ScriptTokKind::Percent: op = ScriptOp::ModI; break;
                default: break;
                }
                break;
            case SType::K::I64:
                switch (expr.Op)
                {
                case ScriptTokKind::Plus: op = ScriptOp::AddL; break;
                case ScriptTokKind::Minus: op = ScriptOp::SubL; break;
                default:
                    Fail(expr, "only + and - exist for i64 in v1.0");
                    return Bad();
                }
                break;
            case SType::K::F32:
            case SType::K::F64:
                switch (expr.Op)
                {
                case ScriptTokKind::Plus: op = ScriptOp::AddF; break;
                case ScriptTokKind::Minus: op = ScriptOp::SubF; break;
                case ScriptTokKind::Star: op = ScriptOp::MulF; break;
                case ScriptTokKind::Slash: op = ScriptOp::DivF; break;
                default: break;
                }
                break;
            default:
                break;
            }
            if (op == ScriptOp::Nop)
            {
                Fail(expr, "operator not defined for these types");
                return Bad();
            }
            const uint32_t dst = Alloc(1);
            Emit(ScriptEncodeABC(op, static_cast<uint8_t>(dst),
                                 static_cast<uint8_t>(lhs.Slot),
                                 static_cast<uint8_t>(rhs.Slot)));
            // f32 op f32 keeps the f32 static type; the value itself lives in
            // f64 precision until a store or explicit f32() rounds it.
            return Make(result, dst);
        }

        Value EvalComparison(const ScriptExpr& expr, Value lhs, Value rhs)
        {
            ScriptOp op = ScriptOp::Nop;
            bool swap = false;
            bool negate = false;
            switch (lhs.Type.Kind)
            {
            case SType::K::I32:
            case SType::K::EnumT:
                switch (expr.Op)
                {
                case ScriptTokKind::EqEq: op = ScriptOp::CEqI; break;
                case ScriptTokKind::NotEq: op = ScriptOp::CEqI; negate = true; break;
                case ScriptTokKind::Lt: op = ScriptOp::CLtI; break;
                case ScriptTokKind::Le: op = ScriptOp::CLeI; break;
                case ScriptTokKind::Gt: op = ScriptOp::CLtI; swap = true; break;
                case ScriptTokKind::Ge: op = ScriptOp::CLeI; swap = true; break;
                default: break;
                }
                break;
            case SType::K::I64:
                switch (expr.Op)
                {
                case ScriptTokKind::EqEq: op = ScriptOp::CEqL; break;
                case ScriptTokKind::NotEq: op = ScriptOp::CEqL; negate = true; break;
                case ScriptTokKind::Lt: op = ScriptOp::CLtL; break;
                case ScriptTokKind::Le: op = ScriptOp::CLeL; break;
                case ScriptTokKind::Gt: op = ScriptOp::CLtL; swap = true; break;
                case ScriptTokKind::Ge: op = ScriptOp::CLeL; swap = true; break;
                default: break;
                }
                break;
            case SType::K::F32:
            case SType::K::F64:
                switch (expr.Op)
                {
                case ScriptTokKind::EqEq: op = ScriptOp::CEqF; break;
                case ScriptTokKind::NotEq: op = ScriptOp::CEqF; negate = true; break;
                case ScriptTokKind::Lt: op = ScriptOp::CLtF; break;
                case ScriptTokKind::Le: op = ScriptOp::CLeF; break;
                case ScriptTokKind::Gt: op = ScriptOp::CLtF; swap = true; break;
                case ScriptTokKind::Ge: op = ScriptOp::CLeF; swap = true; break;
                default: break;
                }
                break;
            case SType::K::Entity:
                if (expr.Op == ScriptTokKind::EqEq) { op = ScriptOp::CEqE; }
                else if (expr.Op == ScriptTokKind::NotEq) { op = ScriptOp::CEqE; negate = true; }
                break;
            case SType::K::Tag:
                if (expr.Op == ScriptTokKind::EqEq) { op = ScriptOp::CEqT; }
                else if (expr.Op == ScriptTokKind::NotEq) { op = ScriptOp::CEqT; negate = true; }
                break;
            case SType::K::Bool:
                if (expr.Op == ScriptTokKind::EqEq) { op = ScriptOp::CEqI; }
                else if (expr.Op == ScriptTokKind::NotEq) { op = ScriptOp::CEqI; negate = true; }
                break;
            default:
                break;
            }
            if (op == ScriptOp::Nop)
            {
                Fail(expr, "comparison not defined for these types");
                return Bad();
            }
            if (swap)
            {
                std::swap(lhs, rhs);
            }
            const uint32_t dst = Alloc(1);
            Emit(ScriptEncodeABC(op, static_cast<uint8_t>(dst),
                                 static_cast<uint8_t>(lhs.Slot),
                                 static_cast<uint8_t>(rhs.Slot)));
            if (negate)
            {
                Emit(ScriptEncodeABC(ScriptOp::Not, static_cast<uint8_t>(dst),
                                     static_cast<uint8_t>(dst), 0));
            }
            return Make({SType::K::Bool}, dst);
        }

        Value EvalVecBinary(const ScriptExpr& expr, Value lhs, Value rhs)
        {
            if (lhs.Type.Is(SType::K::Vec3) && rhs.Type.Is(SType::K::Vec3))
            {
                ScriptOp op = ScriptOp::Nop;
                if (expr.Op == ScriptTokKind::Plus) { op = ScriptOp::VAdd3; }
                if (expr.Op == ScriptTokKind::Minus) { op = ScriptOp::VSub3; }
                if (op == ScriptOp::Nop)
                {
                    Fail(expr, "operator not defined for Vec3");
                    return Bad();
                }
                const uint32_t dst = Alloc(3);
                Emit(ScriptEncodeABC(op, static_cast<uint8_t>(dst),
                                     static_cast<uint8_t>(lhs.Slot),
                                     static_cast<uint8_t>(rhs.Slot)));
                return Make({SType::K::Vec3}, dst);
            }
            // Vec3 * scalar (either order).
            if (expr.Op == ScriptTokKind::Star)
            {
                Value vec = lhs.Type.Is(SType::K::Vec3) ? lhs : rhs;
                Value scalar = lhs.Type.Is(SType::K::Vec3) ? rhs : lhs;
                if (!IsFloat(scalar.Type))
                {
                    Fail(expr, "Vec3 scale expects a float scalar");
                    return Bad();
                }
                const uint32_t dst = Alloc(3);
                Emit(ScriptEncodeABC(ScriptOp::VScale3, static_cast<uint8_t>(dst),
                                     static_cast<uint8_t>(vec.Slot),
                                     static_cast<uint8_t>(scalar.Slot)));
                return Make({SType::K::Vec3}, dst);
            }
            Fail(expr, "operator not defined for these types");
            return Bad();
        }

        Value EvalUnary(const ScriptExpr& expr)
        {
            Value value = EvalExpr(*expr.Lhs, nullptr);
            if (!value.Ok)
            {
                return Bad();
            }
            if (expr.Op == ScriptTokKind::Not)
            {
                if (!value.Type.Is(SType::K::Bool))
                {
                    Fail(expr, "'!' expects a bool");
                    return Bad();
                }
                const uint32_t dst = Alloc(1);
                Emit(ScriptEncodeABC(ScriptOp::Not, static_cast<uint8_t>(dst),
                                     static_cast<uint8_t>(value.Slot), 0));
                return Make({SType::K::Bool}, dst);
            }
            // Unary minus.
            const uint32_t dst = Alloc(1);
            if (value.Type.Is(SType::K::I32))
            {
                Emit(ScriptEncodeABC(ScriptOp::NegI, static_cast<uint8_t>(dst),
                                     static_cast<uint8_t>(value.Slot), 0));
                return Make({SType::K::I32}, dst);
            }
            if (IsFloat(value.Type))
            {
                Emit(ScriptEncodeABC(ScriptOp::NegF, static_cast<uint8_t>(dst),
                                     static_cast<uint8_t>(value.Slot), 0));
                return Make(value.Type, dst);
            }
            Fail(expr, "'-' expects a numeric value");
            return Bad();
        }

        Value EvalIndex(const ScriptExpr& expr)
        {
            Value base = EvalExpr(*expr.Lhs, nullptr);
            if (!base.Ok || !base.Type.Is(SType::K::ArrayT))
            {
                Fail(expr, "indexing expects an array");
                return Bad();
            }
            Value index = EvalExpr(*expr.Rhs, nullptr);
            if (!index.Ok || !index.Type.Is(SType::K::I32))
            {
                Fail(*expr.Rhs, "array index must be i32");
                return Bad();
            }
            const uint32_t dst = Alloc(1);
            Emit(ScriptEncodeABC(ScriptOp::AGetL, static_cast<uint8_t>(dst),
                                 static_cast<uint8_t>(base.Slot), 0));
            Emit(ScriptEncodeExtArray(static_cast<uint16_t>(base.Type.Count),
                                      static_cast<uint8_t>(index.Slot), 1));
            SType eltType;
            eltType.Kind = base.Type.Elt;
            return Make(eltType, dst);
        }

        Value EvalExprImpl(const ScriptExpr& expr, const SType* hint)
        {
            switch (expr.Kind)
            {
            case ScriptExpr::K::IntLit:
            {
                int64_t v = 0;
                const auto [p, ec] = std::from_chars(expr.Text.data(),
                                                     expr.Text.data() + expr.Text.size(), v);
                (void)p;
                SType type = hint != nullptr && hint->Is(SType::K::I64) ? SType{SType::K::I64}
                                                                        : SType{SType::K::I32};
                if (hint != nullptr && (hint->Is(SType::K::F32) || hint->Is(SType::K::F64)))
                {
                    // Integer literal in a float context.
                    type = *hint;
                    return LoadConstBits(type,
                                         RegisterBitsFor(type, static_cast<double>(v), v, false));
                }
                if (ec != std::errc() || (type.Is(SType::K::I32)
                                          && (v < INT32_MIN || v > INT32_MAX)))
                {
                    Fail(expr, "integer literal out of range");
                    return Bad();
                }
                return LoadConstBits(type, RegisterBitsFor(type, 0.0, v, false));
            }
            case ScriptExpr::K::FloatLit:
            {
                const double v = std::stod(std::string(expr.Text));
                const SType type = hint != nullptr && hint->Is(SType::K::F64)
                                       ? SType{SType::K::F64}
                                       : SType{SType::K::F32};
                return LoadConstBits(type, RegisterBitsFor(type, v, 0, false));
            }
            case ScriptExpr::K::BoolLit:
                return LoadConstBits({SType::K::Bool}, expr.Text == "true" ? 1 : 0);
            case ScriptExpr::K::PrefabLit:
            {
                const uint8_t bind = AssetBind(expr.Text, ScriptAssetKind::Prefab);
                Value value = LoadConstBits({SType::K::I32}, bind);
                value.Type = {SType::K::Prefab};
                return value;
            }
            case ScriptExpr::K::AssetLit:
            {
                const uint8_t bind = AssetBind(expr.Text, ScriptAssetKind::Generic);
                Value value = LoadConstBits({SType::K::I32}, bind);
                value.Type = {SType::K::Asset};
                return value;
            }
            case ScriptExpr::K::CueLit:
                Fail(expr, "cue literals are only valid in ctx.cue()");
                return Bad();
            case ScriptExpr::K::TagLit:
                Fail(expr, "tag literals are only valid in tag operations");
                return Bad();
            case ScriptExpr::K::Ident:
                return EvalIdent(expr);
            case ScriptExpr::K::Member:
            {
                // Component field loads route through place resolution; all
                // other members (ctx, records, vector components) evaluate
                // directly. The distinction is syntactic: a component name in
                // the second-to-last or third-to-last position.
                if (IsComponentAccess(expr))
                {
                    Place place = ResolvePlace(expr);
                    if (!place.Ok)
                    {
                        return Bad();
                    }
                    const uint32_t width = Width(place.Type);
                    const uint32_t dst = Alloc(width);
                    Emit(ScriptEncodeABC(ScriptOp::Cld, static_cast<uint8_t>(dst),
                                         static_cast<uint8_t>(place.EntitySlot),
                                         place.FieldBind));
                    return Make(place.Type, dst);
                }
                return EvalMember(expr);
            }
            case ScriptExpr::K::Index:
                return EvalIndex(expr);
            case ScriptExpr::K::Call:
                return EvalCall(expr);
            case ScriptExpr::K::Record:
                Fail(expr, "record literals are only valid in commands.add()");
                return Bad();
            case ScriptExpr::K::Unary:
                return EvalUnary(expr);
            case ScriptExpr::K::Binary:
                return EvalBinary(expr);
            }
            return Bad();
        }

        //────────────────────────────────────────────────────────────────
        // Statements
        //────────────────────────────────────────────────────────────────

        bool EmitAssign(const ScriptStmt& stmt)
        {
            // Compound ops desugar to load-op-store through the same place.
            const bool compound = stmt.Op != ScriptTokKind::Assign;

            // Local array element store: place[index] = value.
            if (stmt.A->Kind == ScriptExpr::K::Index)
            {
                if (compound)
                {
                    return FailAt(stmt.Line, stmt.Col,
                                  "compound assignment to array elements is unsupported");
                }
                Value base = EvalExpr(*stmt.A->Lhs, nullptr);
                if (!base.Ok || !base.Type.Is(SType::K::ArrayT))
                {
                    return FailAt(stmt.Line, stmt.Col, "indexing expects an array");
                }
                Value index = EvalExpr(*stmt.A->Rhs, nullptr);
                if (!index.Ok || !index.Type.Is(SType::K::I32))
                {
                    return FailAt(stmt.Line, stmt.Col, "array index must be i32");
                }
                SType eltType;
                eltType.Kind = base.Type.Elt;
                Value value = EvalExpr(*stmt.B, &eltType);
                if (!value.Ok || !Coerce(value, eltType, *stmt.B))
                {
                    return false;
                }
                Emit(ScriptEncodeABC(ScriptOp::ASetL, static_cast<uint8_t>(base.Slot),
                                     static_cast<uint8_t>(value.Slot), 0));
                Emit(ScriptEncodeExtArray(static_cast<uint16_t>(base.Type.Count),
                                          static_cast<uint8_t>(index.Slot), 1));
                return true;
            }

            Place place = ResolvePlace(*stmt.A);
            if (!place.Ok)
            {
                return false;
            }
            Value value{};
            if (compound)
            {
                // Build the equivalent binary expression over the loaded value.
                ScriptExpr synthetic;
                synthetic.Kind = ScriptExpr::K::Binary;
                synthetic.Line = stmt.Line;
                synthetic.Col = stmt.Col;
                switch (stmt.Op)
                {
                case ScriptTokKind::PlusAssign: synthetic.Op = ScriptTokKind::Plus; break;
                case ScriptTokKind::MinusAssign: synthetic.Op = ScriptTokKind::Minus; break;
                case ScriptTokKind::StarAssign: synthetic.Op = ScriptTokKind::Star; break;
                default: synthetic.Op = ScriptTokKind::Slash; break;
                }
                // Load current value.
                Value current;
                if (place.Kind == Place::K::Local)
                {
                    current = Make(place.Type, place.Slot);
                }
                else
                {
                    const uint32_t width = Width(place.Type);
                    const uint32_t dst = Alloc(width);
                    Emit(ScriptEncodeABC(ScriptOp::Cld, static_cast<uint8_t>(dst),
                                         static_cast<uint8_t>(place.EntitySlot),
                                         place.FieldBind));
                    current = Make(place.Type, dst);
                }
                Value rhs = EvalExpr(*stmt.B, &place.Type);
                if (!rhs.Ok)
                {
                    return false;
                }
                // Reuse the binary machinery via a direct dispatch.
                ScriptExpr lhsProxy;
                lhsProxy.Kind = ScriptExpr::K::Ident;
                lhsProxy.Line = stmt.Line;
                lhsProxy.Col = stmt.Col;
                (void)lhsProxy;
                // Inline: unify and emit.
                Value l = current;
                Value r = rhs;
                if (IsFloat(l.Type) && IsFloat(r.Type) && !(l.Type == r.Type))
                {
                    l.Type = {SType::K::F64};
                    r.Type = {SType::K::F64};
                }
                if (!(l.Type == r.Type))
                {
                    return FailAt(stmt.Line, stmt.Col,
                                  "operand types differ: insert an explicit conversion");
                }
                ScriptOp op = ScriptOp::Nop;
                if (l.Type.Is(SType::K::I32))
                {
                    op = synthetic.Op == ScriptTokKind::Plus ? ScriptOp::AddI
                         : synthetic.Op == ScriptTokKind::Minus ? ScriptOp::SubI
                         : synthetic.Op == ScriptTokKind::Star ? ScriptOp::MulI
                                                               : ScriptOp::DivI;
                }
                else if (IsFloat(l.Type))
                {
                    op = synthetic.Op == ScriptTokKind::Plus ? ScriptOp::AddF
                         : synthetic.Op == ScriptTokKind::Minus ? ScriptOp::SubF
                         : synthetic.Op == ScriptTokKind::Star ? ScriptOp::MulF
                                                               : ScriptOp::DivF;
                }
                else
                {
                    return FailAt(stmt.Line, stmt.Col,
                                  "compound assignment not defined for this type");
                }
                const uint32_t dst = Alloc(1);
                Emit(ScriptEncodeABC(op, static_cast<uint8_t>(dst),
                                     static_cast<uint8_t>(l.Slot),
                                     static_cast<uint8_t>(r.Slot)));
                value = Make(place.Type.Is(SType::K::F32) && l.Type.Is(SType::K::F64)
                                 ? SType{SType::K::F64}
                                 : l.Type,
                             dst);
            }
            else
            {
                value = EvalExpr(*stmt.B, &place.Type);
                if (!value.Ok)
                {
                    return false;
                }
            }

            if (place.Kind == Place::K::Local)
            {
                // Widening into an f64 local is fine; anything else exact.
                if (place.Type.Is(SType::K::F64) && value.Type.Is(SType::K::F32))
                {
                    value.Type = {SType::K::F64};
                }
                if (!(value.Type == place.Type))
                {
                    return FailAt(stmt.Line, stmt.Col,
                                  "type mismatch: insert an explicit conversion");
                }
                CopyTo(place.Slot, value);
                return true;
            }

            // Component store. Widening applies; the runtime rounds f32
            // fields on store (spec D.2).
            if (place.Type.Is(SType::K::F64) && value.Type.Is(SType::K::F32))
            {
                value.Type = {SType::K::F64};
            }
            if (place.Type.Is(SType::K::F32) && value.Type.Is(SType::K::F64))
            {
                value.Type = {SType::K::F32}; // store rounds
            }
            if (!(value.Type == place.Type))
            {
                return FailAt(stmt.Line, stmt.Col,
                              "type mismatch: insert an explicit conversion");
            }
            Emit(ScriptEncodeABC(ScriptOp::Cst, static_cast<uint8_t>(place.EntitySlot),
                                 static_cast<uint8_t>(value.Slot), place.FieldBind));
            return true;
        }

        bool EmitStmt(const ScriptStmt& stmt);

        bool EmitBody(const std::vector<ScriptStmtPtr>& body)
        {
            Scopes.emplace_back();
            const uint32_t savedSlots = NextSlot;
            for (const ScriptStmtPtr& stmt : body)
            {
                if (!EmitStmt(*stmt))
                {
                    return false;
                }
            }
            NextSlot = savedSlots;
            Scopes.pop_back();
            return true;
        }
    };

    Emitter::Value Emitter::EvalExpr(const ScriptExpr& expr, const SType* hint)
    {
        return EvalExprImpl(expr, hint);
    }

    bool Emitter::EmitStmt(const ScriptStmt& stmt)
    {
        Line(stmt.Line, stmt.Col);
        TempScope temps(*this);
        switch (stmt.Kind)
        {
        case ScriptStmt::K::Let:
        {
            SType declared{};
            const SType* hint = nullptr;
            if (stmt.Type)
            {
                if (!ResolveTypeRef(C, Fn.FileIndex, *stmt.Type, declared, false))
                {
                    return false;
                }
                hint = &declared;
            }
            Value value = EvalExpr(*stmt.A, hint);
            if (!value.Ok)
            {
                return false;
            }
            if (hint != nullptr && !Coerce(value, declared, *stmt.A))
            {
                return false;
            }
            if (value.Type.Is(SType::K::Void) || value.Type.Is(SType::K::Namespace)
                || value.Type.Is(SType::K::TypeName) || value.Type.Is(SType::K::Ctx))
            {
                return FailAt(stmt.Line, stmt.Col, "this expression has no storable value");
            }
            const uint32_t width = Width(value.Type);
            uint32_t home;
            if (value.Slot == temps.Saved)
            {
                // The init landed exactly at the statement watermark: claim
                // it in place, no copy.
                home = value.Slot;
            }
            else
            {
                // Borrowed slot (a local, prelude register, or a view into a
                // wider temp): give the local its own home.
                home = Alloc(width);
                CopyTo(home, value);
            }
            // Keep the local (and nothing above it that we can free) alive
            // past this statement.
            temps.Saved = std::max(temps.Saved, home + width);
            Scopes.back()[stmt.Name] = {value.Type, home};
            return true;
        }
        case ScriptStmt::K::Assign:
            return EmitAssign(stmt);
        case ScriptStmt::K::ExprStmt:
        {
            Value value = EvalExpr(*stmt.A, nullptr);
            return value.Ok;
        }
        case ScriptStmt::K::If:
        {
            Value cond = EvalExpr(*stmt.A, nullptr);
            if (!cond.Ok || !cond.Type.Is(SType::K::Bool))
            {
                return FailAt(stmt.Line, stmt.Col, "if condition must be bool");
            }
            const uint32_t skipThen = EmitBranch(ScriptOp::Bf, static_cast<uint8_t>(cond.Slot));
            if (!EmitBody(stmt.Body))
            {
                return false;
            }
            if (!stmt.ElseBody.empty())
            {
                const uint32_t skipElse = EmitBranch(ScriptOp::Jmp);
                Patch(skipThen, Here());
                if (!EmitBody(stmt.ElseBody))
                {
                    return false;
                }
                Patch(skipElse, Here());
            }
            else
            {
                Patch(skipThen, Here());
            }
            return true;
        }
        case ScriptStmt::K::While:
        {
            const uint32_t head = Here();
            Value cond = EvalExpr(*stmt.A, nullptr);
            if (!cond.Ok || !cond.Type.Is(SType::K::Bool))
            {
                return FailAt(stmt.Line, stmt.Col, "while condition must be bool");
            }
            const uint32_t exit = EmitBranch(ScriptOp::Bf, static_cast<uint8_t>(cond.Slot));
            if (!EmitBody(stmt.Body))
            {
                return false;
            }
            EmitJumpTo(head);
            Patch(exit, Here());
            return true;
        }
        case ScriptStmt::K::For:
        {
            Value from = EvalExpr(*stmt.A, nullptr);
            Value to = EvalExpr(*stmt.B, nullptr);
            if (!from.Ok || !to.Ok || !from.Type.Is(SType::K::I32) || !to.Type.Is(SType::K::I32))
            {
                return FailAt(stmt.Line, stmt.Col, "for bounds must be i32");
            }
            // The loop variable and bound persist across iterations.
            const uint32_t i = Alloc(1);
            const uint32_t bound = Alloc(1);
            const uint32_t one = Alloc(1);
            const uint32_t cond = Alloc(1);
            temps.Saved = NextSlot;
            Emit(ScriptEncodeABC(ScriptOp::Mov, static_cast<uint8_t>(i),
                                 static_cast<uint8_t>(from.Slot), 0));
            Emit(ScriptEncodeABC(ScriptOp::Mov, static_cast<uint8_t>(bound),
                                 static_cast<uint8_t>(to.Slot), 0));
            Emit(ScriptEncodeAsBx(ScriptOp::Ldi, static_cast<uint8_t>(one), 1));
            const uint32_t head = Here();
            Emit(ScriptEncodeABC(ScriptOp::CLtI, static_cast<uint8_t>(cond),
                                 static_cast<uint8_t>(i), static_cast<uint8_t>(bound)));
            const uint32_t exit = EmitBranch(ScriptOp::Bf, static_cast<uint8_t>(cond));
            Scopes.emplace_back();
            Scopes.back()[stmt.Name] = {{SType::K::I32}, i};
            for (const ScriptStmtPtr& inner : stmt.Body)
            {
                if (!EmitStmt(*inner))
                {
                    return false;
                }
            }
            Scopes.pop_back();
            Emit(ScriptEncodeABC(ScriptOp::AddI, static_cast<uint8_t>(i),
                                 static_cast<uint8_t>(i), static_cast<uint8_t>(one)));
            EmitJumpTo(head);
            Patch(exit, Here());
            return true;
        }
        case ScriptStmt::K::Return:
        {
            if (stmt.A == nullptr)
            {
                if (!Fn.Return.Is(SType::K::Void))
                {
                    return FailAt(stmt.Line, stmt.Col, "this function returns a value");
                }
                Emit(ScriptEncodeABC(ScriptOp::Ret, 0, 0, 0));
                return true;
            }
            if (Fn.Return.Is(SType::K::Void))
            {
                return FailAt(stmt.Line, stmt.Col, "this function returns nothing");
            }
            Value value = EvalExpr(*stmt.A, &Fn.Return);
            if (!value.Ok || !Coerce(value, Fn.Return, *stmt.A))
            {
                return false;
            }
            Emit(ScriptEncodeABC(ScriptOp::RetV, static_cast<uint8_t>(value.Slot),
                                 static_cast<uint8_t>(Width(Fn.Return)), 0));
            return true;
        }
        case ScriptStmt::K::Enter:
        {
            if (Fn.Block == nullptr
                || (Fn.Block->Kind != ScriptAstBlockKind::Ability
                    && Fn.Block->Kind != ScriptAstBlockKind::Behavior))
            {
                return FailAt(stmt.Line, stmt.Col,
                              "enter is only valid in ability and behavior callbacks");
            }
            for (uint32_t i = 0; i < Fn.Block->States.size(); ++i)
            {
                if (Fn.Block->States[i] == stmt.Name)
                {
                    Emit(ScriptEncodeSAx(ScriptOp::Enter, static_cast<int32_t>(i)));
                    return true;
                }
            }
            return FailAt(stmt.Line, stmt.Col,
                          "unknown state '" + std::string(stmt.Name) + "'");
        }
        }
        return false;
    }

}

//─────────────────────────────────────────────────────────────────────────
// Driver
//─────────────────────────────────────────────────────────────────────────

const ScriptHostDecls& DefaultScriptHostDecls()
{
    // Native LocalTransform stores Transform3f (f32 position/scale), so the
    // script-visible fields are f32 and the field-bind scalar is Float; the
    // world-side host converts f32 storage to the VM's f64 registers.
    static const ScriptHostFieldDecl kTransformFields[] = {
        {"position", "local.position", ScriptScalarKind::Float, 3},
        {"scale", "local.scale", ScriptScalarKind::Float, 3},
    };
    static const ScriptHostComponentDecl kComponents[] = {
        {"Transform", kTransformFields},
    };
    static const ScriptHostDecls kDecls{kComponents};
    return kDecls;
}

namespace
{
    // Path resolution per spec D.8: relative to the importing file, no ".."
    // or absolute paths.
    bool ResolveImportPath(std::string_view importer, std::string_view import, std::string& out)
    {
        if (import.empty() || import.front() == '/')
        {
            return false;
        }
        if (import.find("..") != std::string_view::npos)
        {
            return false;
        }
        const size_t slash = importer.find_last_of('/');
        out = slash == std::string_view::npos
                  ? std::string(import)
                  : std::string(importer.substr(0, slash + 1)) + std::string(import);
        return true;
    }

    struct LoadedUnit
    {
        std::string Path;
        std::unique_ptr<std::string> Source;
        ScriptAstUnit Unit;
    };

    bool LoadUnits(std::string_view mainPath, std::string_view mainSource,
                   const ScriptSourceResolver& resolver, std::vector<LoadedUnit>& out,
                   ScriptCompileResult& result)
    {
        std::set<std::string> visited;
        // Depth-first, imports before the importer, matching the source-hash
        // order in spec section B.
        struct Frame
        {
            std::string Path;
            std::unique_ptr<std::string> Source;
        };

        std::function<bool(const std::string&, std::unique_ptr<std::string>)> load =
            [&](const std::string& path, std::unique_ptr<std::string> source) -> bool {
            if (!visited.insert(path).second)
            {
                return true; // idempotent import
            }
            const ScriptLexResult lexed = LexScript(*source);
            if (!lexed.Ok)
            {
                result.Error = {path, lexed.ErrorLine, lexed.ErrorCol, lexed.Error};
                return false;
            }
            ScriptParseResult parsed = ParseScript(path, lexed.Tokens);
            if (!parsed.Ok)
            {
                result.Error = {path, parsed.ErrorLine, parsed.ErrorCol, parsed.Error};
                return false;
            }
            // Imports first (depth first).
            for (const std::string_view import : parsed.Unit.Imports)
            {
                std::string resolved;
                if (!ResolveImportPath(path, import, resolved))
                {
                    result.Error = {path, 1, 1,
                                    "invalid import path '" + std::string(import) + "'"};
                    return false;
                }
                if (visited.contains(resolved))
                {
                    continue;
                }
                auto text = std::make_unique<std::string>();
                if (!resolver || !resolver(resolved, *text))
                {
                    result.Error = {path, 1, 1, "cannot read import '" + resolved + "'"};
                    return false;
                }
                if (!load(resolved, std::move(text)))
                {
                    return false;
                }
            }
            out.push_back({path, std::move(source), std::move(parsed.Unit)});
            return true;
        };

        return load(std::string(mainPath), std::make_unique<std::string>(mainSource));
    }
}

ScriptCompileResult CompileScript(std::string_view mainPath, std::string_view mainSource,
                                  const ScriptSourceResolver& resolver,
                                  const ScriptHostDecls& hostDecls)
{
    ScriptCompileResult result;

    std::vector<LoadedUnit> units;
    if (!LoadUnits(mainPath, mainSource, resolver, units, result))
    {
        return result;
    }

    Ctx ctx;
    ctx.Host = &hostDecls;
    ctx.Module.HasDebug = true;

    // Source hash over the concatenated unit texts in load order.
    {
        std::string all;
        for (const LoadedUnit& unit : units)
        {
            all += *unit.Source;
        }
        ctx.Module.SourceHash = HashBytes64(std::string_view(all));
    }

    // Host components come first so script names cannot shadow them.
    for (const ScriptHostComponentDecl& host : hostDecls.Components)
    {
        ComponentSem sem;
        sem.Name = host.Name;
        sem.IsHost = true;
        for (const ScriptHostFieldDecl& field : host.Fields)
        {
            ComponentFieldSem fieldSem;
            fieldSem.Name = field.Name;
            fieldSem.EnginePath = std::string(field.EnginePath);
            fieldSem.BackingScalar = field.Scalar;
            fieldSem.SlotCount = field.SlotCount;
            fieldSem.Type = field.SlotCount == 3   ? SType{SType::K::Vec3}
                            : field.SlotCount == 2 ? SType{SType::K::Vec2}
                            : field.SlotCount == 4 ? SType{SType::K::Quat}
                            : field.Scalar == ScriptScalarKind::Double
                                ? SType{SType::K::F64}
                                : SType{SType::K::F32};
            sem.Fields.push_back(std::move(fieldSem));
        }
        ctx.Components.push_back(std::move(sem));
    }

    for (uint16_t u = 0; u < units.size(); ++u)
    {
        units[u].Unit.FileIndex = u;
        ctx.Files.push_back(units[u].Path);
        ctx.Module.DebugFiles.push_back(ctx.Str(units[u].Path));
    }
    for (const LoadedUnit& unit : units)
    {
        if (!CollectUnit(ctx, unit.Unit))
        {
            result.Error = {ctx.Failure->File, ctx.Failure->Line, ctx.Failure->Col,
                            ctx.Failure->Message};
            return result;
        }
    }

    // Block consts/params and callback signatures, then free functions.
    auto resolveFnTypes = [&ctx](FnSem& fn, uint16_t file) -> bool {
        for (const ScriptAstFnParam& param : fn.Ast->Params)
        {
            SType type;
            if (!ResolveTypeRef(ctx, file, param.Type, type, false))
            {
                // Context params resolve specially below.
                const std::string_view name = param.Type.Name;
                if (name == "AbilityContext" || name == "BehaviorContext"
                    || name == "TriggerContext" || name == "InteractionContext")
                {
                    ctx.Failure.reset();
                    type = {SType::K::Ctx, static_cast<uint32_t>(
                        name == "AbilityContext"     ? ScriptAstBlockKind::Ability
                        : name == "BehaviorContext"  ? ScriptAstBlockKind::Behavior
                        : name == "TriggerContext"   ? ScriptAstBlockKind::Trigger
                                                     : ScriptAstBlockKind::Interaction)};
                }
                else
                {
                    return false;
                }
            }
            fn.ParamTypes.push_back(type);
        }
        if (fn.Ast->Return)
        {
            if (!ResolveTypeRef(ctx, file, *fn.Ast->Return, fn.Return, false))
            {
                return false;
            }
        }
        return true;
    };

    for (const LoadedUnit& unit : units)
    {
        for (const ScriptAstFn& fnAst : unit.Unit.FreeFns)
        {
            FnSem fn;
            fn.Ast = &fnAst;
            fn.FileIndex = unit.Unit.FileIndex;
            fn.FullName = std::string(fnAst.Name);
            if (!resolveFnTypes(fn, unit.Unit.FileIndex))
            {
                break;
            }
            uint32_t argSlots = 0;
            for (const SType& type : fn.ParamTypes)
            {
                argSlots += Width(type);
            }
            fn.ArgSlots = static_cast<uint8_t>(argSlots);
            fn.RetSlots = static_cast<uint8_t>(Width(fn.Return));
            ctx.Fns.push_back(std::move(fn));
        }
        for (const ScriptAstBlock& block : unit.Unit.Blocks)
        {
            // Block consts and params.
            for (const ScriptAstConst& decl : block.Consts)
            {
                SType type;
                if (!ResolveTypeRef(ctx, unit.Unit.FileIndex, decl.Type, type, false))
                {
                    break;
                }
                double f = 0.0;
                int64_t i = 0;
                bool b = false;
                if (!EvalConstLiteral(ctx, unit.Unit.FileIndex, *decl.Value, type, f, i, b))
                {
                    break;
                }
                if (decl.IsParam)
                {
                    if (Width(type) != 1)
                    {
                        ctx.Fail(unit.Unit.FileIndex, decl.Line, decl.Col,
                                 "params are scalar in v1.0");
                        break;
                    }
                    ScriptParamDef param;
                    param.Name = ctx.Str(decl.Name);
                    param.Scalar = static_cast<uint8_t>(
                        type.Is(SType::K::F32)   ? ScriptScalarKind::Float
                        : type.Is(SType::K::F64) ? ScriptScalarKind::Double
                        : type.Is(SType::K::I32) ? ScriptScalarKind::Int32
                        : type.Is(SType::K::I64) ? ScriptScalarKind::Int64
                                                 : ScriptScalarKind::Bool);
                    param.SlotCount = 1;
                    param.DefaultRaw.push_back(RegisterBitsFor(type, f, i, b));
                    ctx.Module.Params.push_back(std::move(param));
                    ctx.BlockParams[&block][decl.Name] = {type, ctx.ParamSlotCursor};
                    ctx.ParamSlotCursor += 1;
                }
                else
                {
                    ctx.BlockConsts[&block][decl.Name] = {type, RegisterBitsFor(type, f, i, b)};
                }
            }
            const CallbackShape shape = ShapeFor(block.Kind);
            for (const ScriptAstFn& fnAst : block.Fns)
            {
                FnSem fn;
                fn.Ast = &fnAst;
                fn.Block = &block;
                fn.FileIndex = unit.Unit.FileIndex;
                fn.FullName = std::string(block.Name) + "."
                              + (fnAst.State.empty() ? std::string(fnAst.Name)
                                                     : std::string(fnAst.State) + "."
                                                           + std::string(fnAst.Name));
                if (!CallbackNameValid(block.Kind, fnAst.Name, !fnAst.State.empty()))
                {
                    ctx.Fail(unit.Unit.FileIndex, fnAst.Line, fnAst.Col,
                             "unknown callback '" + std::string(fnAst.Name) + "' for this kind");
                    break;
                }
                if (!fnAst.State.empty())
                {
                    bool known = false;
                    for (const std::string_view state : block.States)
                    {
                        known = known || state == fnAst.State;
                    }
                    if (!known)
                    {
                        ctx.Fail(unit.Unit.FileIndex, fnAst.Line, fnAst.Col,
                                 "unknown state '" + std::string(fnAst.State) + "'");
                        break;
                    }
                }
                if (fnAst.Params.size() != 1
                    || fnAst.Params[0].Type.Name != shape.CtxType)
                {
                    ctx.Fail(unit.Unit.FileIndex, fnAst.Line, fnAst.Col,
                             "callback takes exactly one parameter of type "
                                 + std::string(shape.CtxType));
                    break;
                }
                if (!resolveFnTypes(fn, unit.Unit.FileIndex))
                {
                    break;
                }
                fn.ArgSlots = shape.ArgSlots;
                const bool isCanInteract = block.Kind == ScriptAstBlockKind::Interaction
                                           && fnAst.Name == "can_interact";
                if (isCanInteract)
                {
                    if (!fn.Return.Is(SType::K::Bool))
                    {
                        ctx.Fail(unit.Unit.FileIndex, fnAst.Line, fnAst.Col,
                                 "can_interact must return bool");
                        break;
                    }
                }
                else if (!fn.Return.Is(SType::K::Void))
                {
                    ctx.Fail(unit.Unit.FileIndex, fnAst.Line, fnAst.Col,
                             "callbacks return nothing");
                    break;
                }
                fn.RetSlots = static_cast<uint8_t>(Width(fn.Return));
                ctx.Fns.push_back(std::move(fn));
            }
        }
        if (ctx.Failure)
        {
            break;
        }
    }
    if (ctx.Failure)
    {
        result.Error = {ctx.Failure->File, ctx.Failure->Line, ctx.Failure->Col,
                        ctx.Failure->Message};
        return result;
    }

    for (uint32_t i = 0; i < ctx.Fns.size(); ++i)
    {
        ctx.Fns[i].FunctionIndex = i;
    }

    // Emit every function.
    for (FnSem& fn : ctx.Fns)
    {
        Emitter emitter(ctx, fn);
        // Bind the ctx parameter (or free-function parameters).
        if (fn.Block != nullptr)
        {
            emitter.Scopes.back()[fn.Ast->Params[0].Name] = {fn.ParamTypes[0], 0};
        }
        else
        {
            uint32_t slot = 0;
            for (size_t p = 0; p < fn.ParamTypes.size(); ++p)
            {
                emitter.Scopes.back()[fn.Ast->Params[p].Name] = {fn.ParamTypes[p], slot};
                slot += Width(fn.ParamTypes[p]);
            }
        }
        for (const ScriptStmtPtr& stmt : fn.Ast->Body)
        {
            if (!emitter.EmitStmt(*stmt))
            {
                result.Error = {ctx.Failure->File, ctx.Failure->Line, ctx.Failure->Col,
                                ctx.Failure->Message};
                return result;
            }
        }
        // Implicit trailing return for void functions.
        if (fn.Return.Is(SType::K::Void))
        {
            emitter.Emit(ScriptEncodeABC(ScriptOp::Ret, 0, 0, 0));
        }
        else
        {
            const uint32_t last = emitter.FnCodeStart < ctx.Module.Code.size()
                                      ? ctx.Module.Code.back()
                                      : 0;
            if (ctx.Module.Code.size() == emitter.FnCodeStart
                || ScriptDecodeOp(last) != ScriptOp::RetV)
            {
                result.Error = {ctx.Files[fn.FileIndex], fn.Ast->Line, fn.Ast->Col,
                                "function must end with a return"};
                return result;
            }
        }
        if (emitter.MaxSlot > kScriptMaxFrameSlots)
        {
            result.Error = {ctx.Files[fn.FileIndex], fn.Ast->Line, fn.Ast->Col,
                            "function needs too many registers: simplify it"};
            return result;
        }

        ScriptFunctionDef def;
        def.Name = ctx.Str(fn.FullName);
        def.CodeOffset = emitter.FnCodeStart;
        def.WordCount = static_cast<uint32_t>(ctx.Module.Code.size()) - emitter.FnCodeStart;
        def.FrameSlots = static_cast<uint8_t>(std::max<uint32_t>(emitter.MaxSlot, 1));
        def.ArgSlots = fn.ArgSlots;
        def.RetSlots = fn.RetSlots;
        def.Flags = fn.Ast->State.empty() ? 0 : kScriptFunctionFlagStateCallback;
        ctx.Module.Functions.push_back(def);
    }

    // Component schemas (script components only).
    for (const ComponentSem& component : ctx.Components)
    {
        if (component.IsHost)
        {
            continue;
        }
        ScriptComponentDef def;
        def.Name = ctx.Str(component.Name);
        for (const ComponentLeaf& leaf : component.Leaves)
        {
            ScriptComponentField field;
            field.Name = ctx.Str(leaf.Path);
            field.Scalar = static_cast<uint8_t>(leaf.Scalar);
            field.ArrayCount = leaf.ArrayCount;
            field.ByteOffset = leaf.ByteOffset;
            field.DefaultRaw = leaf.DefaultScalarRaw;
            def.Fields.push_back(field);
        }
        ctx.Module.Components.push_back(std::move(def));
    }

    // Declarations.
    for (const LoadedUnit& unit : units)
    {
        for (const ScriptAstBlock& block : unit.Unit.Blocks)
        {
            ScriptDeclaration decl;
            decl.Name = ctx.Str(block.Name);
            decl.Kind = static_cast<ScriptDeclKind>(static_cast<uint8_t>(block.Kind) + 1);
            for (const std::string_view state : block.States)
            {
                decl.States.push_back(ctx.Str(state));
            }
            for (const FnSem& fn : ctx.Fns)
            {
                if (fn.Block == &block)
                {
                    const std::string callbackName =
                        fn.Ast->State.empty()
                            ? std::string(fn.Ast->Name)
                            : std::string(fn.Ast->State) + "." + std::string(fn.Ast->Name);
                    decl.Callbacks.push_back({ctx.Str(callbackName), fn.FunctionIndex});
                }
            }
            ctx.Module.Declarations.push_back(std::move(decl));
        }
    }

    result.Ok = true;
    result.Module = std::move(ctx.Module);
    return result;
}
