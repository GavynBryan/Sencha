#include "ScriptParser.h"

namespace
{
    struct Parser
    {
        const std::vector<ScriptToken>& Tokens;
        size_t At = 0;
        ScriptParseResult& Result;
        bool Failed = false;
        // Record literals are legal everywhere an expression is expected
        // except control-flow conditions, where `ident {` must read as the
        // condition followed by the body block.
        bool AllowRecordLit = true;

        [[nodiscard]] const ScriptToken& Peek(size_t ahead = 0) const
        {
            const size_t index = At + ahead;
            return index < Tokens.size() ? Tokens[index] : Tokens.back();
        }

        [[nodiscard]] bool Is(ScriptTokKind kind) const { return Peek().Kind == kind; }

        const ScriptToken& Advance() { return Tokens[At < Tokens.size() - 1 ? At++ : At]; }

        bool Fail(const ScriptToken& token, std::string message)
        {
            if (!Failed)
            {
                Failed = true;
                Result.Error = std::move(message);
                Result.ErrorLine = token.Line;
                Result.ErrorCol = token.Col;
            }
            return false;
        }

        bool Expect(ScriptTokKind kind, const char* what)
        {
            if (!Is(kind))
            {
                return Fail(Peek(), std::string("expected ") + what);
            }
            Advance();
            return true;
        }

        void SkipTerminators()
        {
            while (Is(ScriptTokKind::Terminator))
            {
                Advance();
            }
        }

        // ── Types ───────────────────────────────────────────────────────

        bool ParseType(ScriptTypeRef& out)
        {
            out.Line = Peek().Line;
            out.Col = Peek().Col;
            if (Is(ScriptTokKind::LBracket))
            {
                Advance();
                if (!Is(ScriptTokKind::Ident))
                {
                    return Fail(Peek(), "expected element type name");
                }
                out.Name = Advance().Text;
                out.IsArray = true;
                if (!Expect(ScriptTokKind::Terminator, "';'"))
                {
                    return false;
                }
                if (!Is(ScriptTokKind::IntLit))
                {
                    return Fail(Peek(), "expected array length");
                }
                out.ArrayCount = static_cast<uint32_t>(std::stoul(std::string(Advance().Text)));
                return Expect(ScriptTokKind::RBracket, "']'");
            }
            if (!Is(ScriptTokKind::Ident))
            {
                return Fail(Peek(), "expected type name");
            }
            out.Name = Advance().Text;
            return true;
        }

        // ── Expressions ─────────────────────────────────────────────────

        ScriptExprPtr Make(ScriptExpr::K kind, const ScriptToken& token)
        {
            auto expr = std::make_unique<ScriptExpr>();
            expr->Kind = kind;
            expr->Line = token.Line;
            expr->Col = token.Col;
            expr->Text = token.Text;
            return expr;
        }

        ScriptExprPtr ParsePrimary()
        {
            const ScriptToken& token = Peek();
            switch (token.Kind)
            {
            case ScriptTokKind::IntLit:
                Advance();
                return Make(ScriptExpr::K::IntLit, token);
            case ScriptTokKind::FloatLit:
                Advance();
                return Make(ScriptExpr::K::FloatLit, token);
            case ScriptTokKind::KwTrue:
            case ScriptTokKind::KwFalse:
                Advance();
                return Make(ScriptExpr::K::BoolLit, token);
            case ScriptTokKind::TagLit:
                Advance();
                return Make(ScriptExpr::K::TagLit, token);
            case ScriptTokKind::CueLit:
                Advance();
                return Make(ScriptExpr::K::CueLit, token);
            case ScriptTokKind::PrefabLit:
                Advance();
                return Make(ScriptExpr::K::PrefabLit, token);
            case ScriptTokKind::AssetLit:
                Advance();
                return Make(ScriptExpr::K::AssetLit, token);
            case ScriptTokKind::LParen:
            {
                Advance();
                const bool saved = AllowRecordLit;
                AllowRecordLit = true;
                ScriptExprPtr inner = ParseExpr();
                AllowRecordLit = saved;
                if (!inner || !Expect(ScriptTokKind::RParen, "')'"))
                {
                    return nullptr;
                }
                return inner;
            }
            case ScriptTokKind::Ident:
            {
                Advance();
                if (AllowRecordLit && Is(ScriptTokKind::LBrace))
                {
                    return ParseRecordLit(token);
                }
                return Make(ScriptExpr::K::Ident, token);
            }
            default:
                Fail(token, "expected an expression");
                return nullptr;
            }
        }

        ScriptExprPtr ParseRecordLit(const ScriptToken& nameToken)
        {
            ScriptExprPtr record = Make(ScriptExpr::K::Record, nameToken);
            Advance(); // {
            SkipTerminators();
            while (!Is(ScriptTokKind::RBrace))
            {
                if (!Is(ScriptTokKind::Ident))
                {
                    Fail(Peek(), "expected field name in record literal");
                    return nullptr;
                }
                record->Names.push_back(Advance().Text);
                if (!Expect(ScriptTokKind::Colon, "':'"))
                {
                    return nullptr;
                }
                ScriptExprPtr value = ParseExpr();
                if (!value)
                {
                    return nullptr;
                }
                record->Args.push_back(std::move(value));
                SkipTerminators();
                if (Is(ScriptTokKind::Comma))
                {
                    Advance();
                    SkipTerminators();
                }
                else
                {
                    break;
                }
            }
            if (!Expect(ScriptTokKind::RBrace, "'}'"))
            {
                return nullptr;
            }
            return record;
        }

        ScriptExprPtr ParsePostfix()
        {
            ScriptExprPtr expr = ParsePrimary();
            while (expr)
            {
                if (Is(ScriptTokKind::Dot))
                {
                    Advance();
                    if (!Is(ScriptTokKind::Ident))
                    {
                        Fail(Peek(), "expected member name after '.'");
                        return nullptr;
                    }
                    const ScriptToken& name = Advance();
                    ScriptExprPtr member = Make(ScriptExpr::K::Member, name);
                    member->Lhs = std::move(expr);
                    expr = std::move(member);
                }
                else if (Is(ScriptTokKind::LBracket))
                {
                    const ScriptToken& bracket = Advance();
                    ScriptExprPtr index = Make(ScriptExpr::K::Index, bracket);
                    const bool saved = AllowRecordLit;
                    AllowRecordLit = true;
                    index->Rhs = ParseExpr();
                    AllowRecordLit = saved;
                    if (!index->Rhs || !Expect(ScriptTokKind::RBracket, "']'"))
                    {
                        return nullptr;
                    }
                    index->Lhs = std::move(expr);
                    expr = std::move(index);
                }
                else if (Is(ScriptTokKind::LParen))
                {
                    const ScriptToken& paren = Advance();
                    ScriptExprPtr call = Make(ScriptExpr::K::Call, paren);
                    call->Lhs = std::move(expr);
                    const bool saved = AllowRecordLit;
                    AllowRecordLit = true;
                    SkipTerminators();
                    while (!Is(ScriptTokKind::RParen))
                    {
                        ScriptExprPtr arg = ParseExpr();
                        if (!arg)
                        {
                            return nullptr;
                        }
                        call->Args.push_back(std::move(arg));
                        SkipTerminators();
                        if (Is(ScriptTokKind::Comma))
                        {
                            Advance();
                            SkipTerminators();
                        }
                        else
                        {
                            break;
                        }
                    }
                    AllowRecordLit = saved;
                    if (!Expect(ScriptTokKind::RParen, "')'"))
                    {
                        return nullptr;
                    }
                    expr = std::move(call);
                }
                else
                {
                    break;
                }
            }
            return expr;
        }

        ScriptExprPtr ParseUnary()
        {
            if (Is(ScriptTokKind::Not) || Is(ScriptTokKind::Minus))
            {
                const ScriptToken& op = Advance();
                ScriptExprPtr expr = Make(ScriptExpr::K::Unary, op);
                expr->Op = op.Kind;
                expr->Lhs = ParseUnary();
                if (!expr->Lhs)
                {
                    return nullptr;
                }
                return expr;
            }
            return ParsePostfix();
        }

        [[nodiscard]] static int Precedence(ScriptTokKind kind)
        {
            switch (kind)
            {
            case ScriptTokKind::Star: case ScriptTokKind::Slash:
            case ScriptTokKind::Percent:
                return 5;
            case ScriptTokKind::Plus: case ScriptTokKind::Minus:
                return 4;
            case ScriptTokKind::EqEq: case ScriptTokKind::NotEq:
            case ScriptTokKind::Lt: case ScriptTokKind::Le:
            case ScriptTokKind::Gt: case ScriptTokKind::Ge:
                return 3;
            case ScriptTokKind::AndAnd:
                return 2;
            case ScriptTokKind::OrOr:
                return 1;
            default:
                return 0;
            }
        }

        ScriptExprPtr ParseBinary(int minPrecedence)
        {
            ScriptExprPtr lhs = ParseUnary();
            while (lhs)
            {
                const int precedence = Precedence(Peek().Kind);
                if (precedence == 0 || precedence < minPrecedence)
                {
                    break;
                }
                const ScriptToken& op = Advance();
                ScriptExprPtr rhs = ParseBinary(precedence + 1);
                if (!rhs)
                {
                    return nullptr;
                }
                ScriptExprPtr node = Make(ScriptExpr::K::Binary, op);
                node->Op = op.Kind;
                node->Lhs = std::move(lhs);
                node->Rhs = std::move(rhs);
                lhs = std::move(node);
            }
            return lhs;
        }

        ScriptExprPtr ParseExpr() { return ParseBinary(1); }

        // ── Statements ──────────────────────────────────────────────────

        ScriptStmtPtr MakeStmt(ScriptStmt::K kind, const ScriptToken& token)
        {
            auto stmt = std::make_unique<ScriptStmt>();
            stmt->Kind = kind;
            stmt->Line = token.Line;
            stmt->Col = token.Col;
            return stmt;
        }

        bool ParseBlockBody(std::vector<ScriptStmtPtr>& out)
        {
            if (!Expect(ScriptTokKind::LBrace, "'{'"))
            {
                return false;
            }
            SkipTerminators();
            while (!Is(ScriptTokKind::RBrace))
            {
                if (Is(ScriptTokKind::Eof))
                {
                    return Fail(Peek(), "unterminated block");
                }
                ScriptStmtPtr stmt = ParseStmt();
                if (!stmt)
                {
                    return false;
                }
                out.push_back(std::move(stmt));
                SkipTerminators();
            }
            Advance(); // }
            return true;
        }

        ScriptStmtPtr ParseIf(const ScriptToken& keyword)
        {
            ScriptStmtPtr stmt = MakeStmt(ScriptStmt::K::If, keyword);
            AllowRecordLit = false;
            stmt->A = ParseExpr();
            AllowRecordLit = true;
            if (!stmt->A || !ParseBlockBody(stmt->Body))
            {
                return nullptr;
            }
            // `else` may start its own line.
            size_t lookahead = At;
            while (lookahead < Tokens.size() && Tokens[lookahead].Kind == ScriptTokKind::Terminator)
            {
                ++lookahead;
            }
            if (lookahead < Tokens.size() && Tokens[lookahead].Kind == ScriptTokKind::KwElse)
            {
                At = lookahead;
                Advance(); // else
                if (Is(ScriptTokKind::KwIf))
                {
                    const ScriptToken& elifToken = Advance();
                    ScriptStmtPtr elif = ParseIf(elifToken);
                    if (!elif)
                    {
                        return nullptr;
                    }
                    stmt->ElseBody.push_back(std::move(elif));
                }
                else if (!ParseBlockBody(stmt->ElseBody))
                {
                    return nullptr;
                }
            }
            return stmt;
        }

        ScriptStmtPtr ParseStmt()
        {
            const ScriptToken& token = Peek();
            switch (token.Kind)
            {
            case ScriptTokKind::KwLet:
            {
                Advance();
                ScriptStmtPtr stmt = MakeStmt(ScriptStmt::K::Let, token);
                if (!Is(ScriptTokKind::Ident))
                {
                    Fail(Peek(), "expected variable name after 'let'");
                    return nullptr;
                }
                stmt->Name = Advance().Text;
                if (Is(ScriptTokKind::Colon))
                {
                    Advance();
                    ScriptTypeRef type;
                    if (!ParseType(type))
                    {
                        return nullptr;
                    }
                    stmt->Type = type;
                }
                if (!Expect(ScriptTokKind::Assign, "'=' in let"))
                {
                    return nullptr;
                }
                stmt->A = ParseExpr();
                return stmt->A ? std::move(stmt) : nullptr;
            }
            case ScriptTokKind::KwIf:
                Advance();
                return ParseIf(token);
            case ScriptTokKind::KwWhile:
            {
                Advance();
                ScriptStmtPtr stmt = MakeStmt(ScriptStmt::K::While, token);
                AllowRecordLit = false;
                stmt->A = ParseExpr();
                AllowRecordLit = true;
                if (!stmt->A || !ParseBlockBody(stmt->Body))
                {
                    return nullptr;
                }
                return stmt;
            }
            case ScriptTokKind::KwFor:
            {
                Advance();
                ScriptStmtPtr stmt = MakeStmt(ScriptStmt::K::For, token);
                if (!Is(ScriptTokKind::Ident))
                {
                    Fail(Peek(), "expected loop variable after 'for'");
                    return nullptr;
                }
                stmt->Name = Advance().Text;
                if (!Expect(ScriptTokKind::KwIn, "'in'"))
                {
                    return nullptr;
                }
                AllowRecordLit = false;
                stmt->A = ParseExpr();
                if (!stmt->A || !Expect(ScriptTokKind::DotDot, "'..'"))
                {
                    AllowRecordLit = true;
                    return nullptr;
                }
                stmt->B = ParseExpr();
                AllowRecordLit = true;
                if (!stmt->B || !ParseBlockBody(stmt->Body))
                {
                    return nullptr;
                }
                return stmt;
            }
            case ScriptTokKind::KwReturn:
            {
                Advance();
                ScriptStmtPtr stmt = MakeStmt(ScriptStmt::K::Return, token);
                if (!Is(ScriptTokKind::Terminator) && !Is(ScriptTokKind::RBrace)
                    && !Is(ScriptTokKind::Eof))
                {
                    stmt->A = ParseExpr();
                    if (!stmt->A)
                    {
                        return nullptr;
                    }
                }
                return stmt;
            }
            case ScriptTokKind::KwEnter:
            {
                Advance();
                ScriptStmtPtr stmt = MakeStmt(ScriptStmt::K::Enter, token);
                if (!Is(ScriptTokKind::Ident))
                {
                    Fail(Peek(), "expected state name after 'enter'");
                    return nullptr;
                }
                stmt->Name = Advance().Text;
                return stmt;
            }
            default:
                break;
            }

            // Expression statement or assignment.
            ScriptExprPtr place = ParseExpr();
            if (!place)
            {
                return nullptr;
            }
            const ScriptToken& next = Peek();
            switch (next.Kind)
            {
            case ScriptTokKind::Assign:
            case ScriptTokKind::PlusAssign:
            case ScriptTokKind::MinusAssign:
            case ScriptTokKind::StarAssign:
            case ScriptTokKind::SlashAssign:
            {
                Advance();
                ScriptStmtPtr stmt = MakeStmt(ScriptStmt::K::Assign, next);
                stmt->Op = next.Kind;
                stmt->A = std::move(place);
                stmt->B = ParseExpr();
                return stmt->B ? std::move(stmt) : nullptr;
            }
            default:
            {
                if (place->Kind != ScriptExpr::K::Call)
                {
                    Fail(*Loc(place.get()), "an expression statement must be a call");
                    return nullptr;
                }
                ScriptStmtPtr stmt = MakeStmt(ScriptStmt::K::ExprStmt, next);
                stmt->Line = place->Line;
                stmt->Col = place->Col;
                stmt->A = std::move(place);
                return stmt;
            }
            }
        }

        [[nodiscard]] const ScriptToken* Loc(const ScriptExpr* expr) const
        {
            static ScriptToken fallback;
            fallback.Line = expr->Line;
            fallback.Col = expr->Col;
            return &fallback;
        }

        // ── Declarations ────────────────────────────────────────────────

        bool ParseConstOrParam(std::vector<ScriptAstConst>& out, bool isParam)
        {
            const ScriptToken& keyword = Advance(); // const / param
            ScriptAstConst decl;
            decl.IsParam = isParam;
            decl.Line = keyword.Line;
            decl.Col = keyword.Col;
            if (!Is(ScriptTokKind::Ident))
            {
                return Fail(Peek(), "expected name");
            }
            decl.Name = Advance().Text;
            if (!Expect(ScriptTokKind::Colon, "':' (const and param declarations are fully annotated)"))
            {
                return false;
            }
            if (!ParseType(decl.Type))
            {
                return false;
            }
            if (!Expect(ScriptTokKind::Assign, "'='"))
            {
                return false;
            }
            decl.Value = ParseExpr();
            if (!decl.Value)
            {
                return false;
            }
            out.push_back(std::move(decl));
            return true;
        }

        bool ParseFn(std::vector<ScriptAstFn>& out)
        {
            const ScriptToken& keyword = Advance(); // fn
            ScriptAstFn fn;
            fn.Line = keyword.Line;
            fn.Col = keyword.Col;
            if (!Is(ScriptTokKind::Ident))
            {
                return Fail(Peek(), "expected function name");
            }
            fn.Name = Advance().Text;
            if (Is(ScriptTokKind::Dot))
            {
                Advance();
                if (!Is(ScriptTokKind::Ident))
                {
                    return Fail(Peek(), "expected callback name after state qualifier");
                }
                fn.State = fn.Name;
                fn.Name = Advance().Text;
            }
            if (!Expect(ScriptTokKind::LParen, "'('"))
            {
                return false;
            }
            while (!Is(ScriptTokKind::RParen))
            {
                ScriptAstFnParam param;
                if (!Is(ScriptTokKind::Ident))
                {
                    return Fail(Peek(), "expected parameter name");
                }
                param.Name = Advance().Text;
                if (!Expect(ScriptTokKind::Colon, "':' (parameters are fully annotated)"))
                {
                    return false;
                }
                if (!ParseType(param.Type))
                {
                    return false;
                }
                fn.Params.push_back(param);
                if (Is(ScriptTokKind::Comma))
                {
                    Advance();
                }
                else
                {
                    break;
                }
            }
            if (!Expect(ScriptTokKind::RParen, "')'"))
            {
                return false;
            }
            if (Is(ScriptTokKind::Arrow))
            {
                Advance();
                ScriptTypeRef type;
                if (!ParseType(type))
                {
                    return false;
                }
                fn.Return = type;
            }
            if (!ParseBlockBody(fn.Body))
            {
                return false;
            }
            out.push_back(std::move(fn));
            return true;
        }

        bool ParseComponent(ScriptAstUnit& unit)
        {
            const ScriptToken& keyword = Advance(); // component
            ScriptAstComponent component;
            component.Line = keyword.Line;
            component.Col = keyword.Col;
            if (!Is(ScriptTokKind::Ident))
            {
                return Fail(Peek(), "expected component name");
            }
            component.Name = Advance().Text;
            if (!Expect(ScriptTokKind::LBrace, "'{'"))
            {
                return false;
            }
            SkipTerminators();
            while (!Is(ScriptTokKind::RBrace))
            {
                ScriptAstField field;
                field.Line = Peek().Line;
                field.Col = Peek().Col;
                if (!Is(ScriptTokKind::Ident))
                {
                    return Fail(Peek(), "expected field name");
                }
                field.Name = Advance().Text;
                if (!Expect(ScriptTokKind::Colon, "':'"))
                {
                    return false;
                }
                if (!ParseType(field.Type))
                {
                    return false;
                }
                if (Is(ScriptTokKind::Assign))
                {
                    Advance();
                    field.Default = ParseExpr();
                    if (!field.Default)
                    {
                        return false;
                    }
                }
                component.Fields.push_back(std::move(field));
                SkipTerminators();
            }
            Advance(); // }
            unit.Components.push_back(std::move(component));
            return true;
        }

        bool ParseEnum(ScriptAstUnit& unit)
        {
            const ScriptToken& keyword = Advance(); // enum
            ScriptAstEnum decl;
            decl.Line = keyword.Line;
            decl.Col = keyword.Col;
            if (!Is(ScriptTokKind::Ident))
            {
                return Fail(Peek(), "expected enum name");
            }
            decl.Name = Advance().Text;
            if (!Expect(ScriptTokKind::LBrace, "'{'"))
            {
                return false;
            }
            SkipTerminators();
            while (!Is(ScriptTokKind::RBrace))
            {
                if (!Is(ScriptTokKind::Ident))
                {
                    return Fail(Peek(), "expected enum member");
                }
                decl.Members.push_back(Advance().Text);
                SkipTerminators();
                if (Is(ScriptTokKind::Comma))
                {
                    Advance();
                    SkipTerminators();
                }
            }
            Advance(); // }
            unit.Enums.push_back(std::move(decl));
            return true;
        }

        bool ParseScriptBlock(ScriptAstUnit& unit, ScriptAstBlockKind kind)
        {
            const ScriptToken& keyword = Advance();
            ScriptAstBlock block;
            block.Kind = kind;
            block.Line = keyword.Line;
            block.Col = keyword.Col;
            if (!Is(ScriptTokKind::Ident))
            {
                return Fail(Peek(), "expected declaration name");
            }
            block.Name = Advance().Text;
            if (!Expect(ScriptTokKind::LBrace, "'{'"))
            {
                return false;
            }
            SkipTerminators();
            while (!Is(ScriptTokKind::RBrace))
            {
                switch (Peek().Kind)
                {
                case ScriptTokKind::KwState:
                    Advance();
                    if (!Is(ScriptTokKind::Ident))
                    {
                        return Fail(Peek(), "expected state name");
                    }
                    block.States.push_back(Advance().Text);
                    break;
                case ScriptTokKind::KwConst:
                    if (!ParseConstOrParam(block.Consts, false))
                    {
                        return false;
                    }
                    break;
                case ScriptTokKind::KwParam:
                    if (!ParseConstOrParam(block.Consts, true))
                    {
                        return false;
                    }
                    break;
                case ScriptTokKind::KwFn:
                    if (!ParseFn(block.Fns))
                    {
                        return false;
                    }
                    break;
                case ScriptTokKind::Eof:
                    return Fail(Peek(), "unterminated declaration block");
                default:
                    return Fail(Peek(), "expected state, const, param, or fn");
                }
                SkipTerminators();
            }
            Advance(); // }
            unit.Blocks.push_back(std::move(block));
            return true;
        }
    };
}

ScriptParseResult ParseScript(std::string_view path, const std::vector<ScriptToken>& tokens)
{
    ScriptParseResult result;
    result.Unit.Path = std::string(path);
    Parser parser{tokens, 0, result};

    parser.SkipTerminators();
    while (!parser.Is(ScriptTokKind::Eof))
    {
        bool ok = true;
        switch (parser.Peek().Kind)
        {
        case ScriptTokKind::KwImport:
            parser.Advance();
            if (!parser.Is(ScriptTokKind::StringLit))
            {
                ok = parser.Fail(parser.Peek(), "expected import path string");
                break;
            }
            result.Unit.Imports.push_back(parser.Advance().Text);
            break;
        case ScriptTokKind::KwComponent:
            ok = parser.ParseComponent(result.Unit);
            break;
        case ScriptTokKind::KwEnum:
            ok = parser.ParseEnum(result.Unit);
            break;
        case ScriptTokKind::KwConst:
            ok = parser.ParseConstOrParam(result.Unit.Consts, false);
            break;
        case ScriptTokKind::KwFn:
            ok = parser.ParseFn(result.Unit.FreeFns);
            break;
        case ScriptTokKind::KwAbility:
            ok = parser.ParseScriptBlock(result.Unit, ScriptAstBlockKind::Ability);
            break;
        case ScriptTokKind::KwBehavior:
            ok = parser.ParseScriptBlock(result.Unit, ScriptAstBlockKind::Behavior);
            break;
        case ScriptTokKind::KwTrigger:
            ok = parser.ParseScriptBlock(result.Unit, ScriptAstBlockKind::Trigger);
            break;
        case ScriptTokKind::KwInteraction:
            ok = parser.ParseScriptBlock(result.Unit, ScriptAstBlockKind::Interaction);
            break;
        default:
            ok = parser.Fail(parser.Peek(),
                             "expected a declaration (import, component, enum, const, fn, "
                             "ability, behavior, trigger, or interaction)");
            break;
        }
        if (!ok)
        {
            return result;
        }
        parser.SkipTerminators();
    }
    result.Ok = true;
    return result;
}
