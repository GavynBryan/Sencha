#include "../CandidateOperationSupport.h"

#include <authored/AuthoredLiteral.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/AuthoredQueryRegistry.h>

#include <format>

namespace candidate_operation
{
    namespace
    {
        struct AuthoredQueryPrepared
        {
            std::string Query;
            std::string EntityArgument;
            JsonValue Constants;
        };

        struct AuthoredQueryBound
        {
            AuthoredQueryHandle Query;
            AuthoredArguments Constants;
            std::size_t EntitySlot = 0;
        };

        CandidatePrepared PrepareAuthoredQuery(const JsonValue& arguments, const CandidatePrepareContext&,
                                               std::string_view, std::vector<std::string>&)
        {
            const JsonValue* constants = arguments.Find("arguments");
            return Prepared(AuthoredQueryPrepared{ std::string(StringOr(arguments, "query", "")),
                                                   std::string(StringOr(arguments, "entity_argument", "")),
                                                   constants != nullptr ? *constants : JsonValue(JsonValue::Object{}) });
        }

        [[nodiscard]] bool IsNumericResult(const DataFieldSchema& result)
        {
            return result.Kind == DataFieldKind::Bool || result.Kind == DataFieldKind::Int
                || result.Kind == DataFieldKind::Float;
        }

        // The query's constants compile against its own declaration, exactly
        // as a verb binding's literals do, so both fail the same way.
        std::shared_ptr<const void> BindAuthoredQuery(const void* prepared, const CandidateBindEnvironment& environment,
                                                      std::string_view subject, std::vector<std::string>& errors)
        {
            const auto& authored = *static_cast<const AuthoredQueryPrepared*>(prepared);
            if (environment.Queries == nullptr)
            {
                errors.push_back(std::format("{}: no authored query catalog to resolve '{}' against", subject, authored.Query));
                return nullptr;
            }
            AuthoredQueryBound bound;
            bound.Query = environment.Queries->Resolve(authored.Query);
            if (!bound.Query.IsValid())
            {
                errors.push_back(std::format("{}: '{}' is not a query this World declares", subject, authored.Query));
                return nullptr;
            }
            const AuthoredQueryDefinition& definition = *environment.Queries->Get(bound.Query.Slot);
            if (!IsNumericResult(definition.Result))
            {
                errors.push_back(std::format("{}: '{}' answers with a value no measure can rank", subject, authored.Query));
                return nullptr;
            }

            const std::vector<DataFieldSchema>& declared = definition.Arguments.Children;
            bool ok = true;
            bool foundEntity = false;
            bound.Constants.Resize(declared.size());
            for (std::size_t slot = 0; slot < declared.size(); ++slot)
            {
                const DataFieldSchema& field = declared[slot];
                if (field.Key == authored.EntityArgument)
                {
                    foundEntity = field.Kind == DataFieldKind::Entity;
                    bound.EntitySlot = slot;
                    continue;
                }
                AuthoredValue value;
                const JsonValue* literal = authored.Constants.Find(field.Key);
                ok = (literal != nullptr ? CompileAuthoredLiteral(*literal, field, subject, field.Key, value, errors)
                                         : CompileAuthoredDefault(field, subject, field.Key, value, errors))
                    && ok;
                bound.Constants.Set(slot, std::move(value));
            }
            if (!foundEntity)
            {
                errors.push_back(std::format("{}: '{}' declares no entity argument '{}'", subject, authored.Query,
                                             authored.EntityArgument));
                ok = false;
            }
            for (const auto& [key, unused] : authored.Constants.AsObject())
            {
                (void)unused;
                if (FindChild(definition.Arguments, key) == nullptr)
                {
                    errors.push_back(std::format("{}: '{}' declares no argument '{}'", subject, authored.Query, key));
                    ok = false;
                }
            }
            if (!ok)
                return nullptr;
            return std::make_shared<const AuthoredQueryBound>(std::move(bound));
        }

        [[nodiscard]] CandidateMeasureStatus FromQuery(AuthoredQueryStatus status)
        {
            switch (status)
            {
            case AuthoredQueryStatus::Value: return CandidateMeasureStatus::Ok;
            case AuthoredQueryStatus::Unavailable: return CandidateMeasureStatus::NotApplicable;
            case AuthoredQueryStatus::Stale:
            case AuthoredQueryStatus::Unbound: return CandidateMeasureStatus::Stale;
            case AuthoredQueryStatus::InvalidArguments:
            case AuthoredQueryStatus::InvalidResult: return CandidateMeasureStatus::Failed;
            }
            return CandidateMeasureStatus::Failed;
        }

        [[nodiscard]] float NumericValue(const AuthoredValue& answer)
        {
            bool flag = false;
            std::int64_t whole = 0;
            double number = 0.0;
            if (answer.TryGetBool(flag))
                return flag ? 1.0f : 0.0f;
            if (answer.TryGetInt(whole))
                return static_cast<float>(whole);
            (void)answer.TryGetFloat(number);
            return static_cast<float>(number);
        }

        void MeasureAuthoredQuery(CandidateRun& run, const void* state, std::span<const std::uint32_t> rows,
                                  std::span<float> values, std::span<CandidateMeasureStatus> statuses)
        {
            const auto& bound = *static_cast<const AuthoredQueryBound*>(state);
            const AuthoredQueryDispatcher* dispatcher = run.Queries();
            if (dispatcher == nullptr)
            {
                for (const std::uint32_t row : rows)
                    statuses[row] = CandidateMeasureStatus::NotApplicable;
                return;
            }

            AuthoredArguments& arguments = run.QueryArguments();
            arguments.Resize(bound.Constants.Size());
            for (std::size_t slot = 0; slot < bound.Constants.Size(); ++slot)
            {
                if (slot != bound.EntitySlot)
                    arguments.Set(slot, bound.Constants.At(slot));
            }
            AuthoredValue& answer = run.QueryAnswer();
            for (const std::uint32_t row : rows)
            {
                arguments.Set(bound.EntitySlot, AuthoredValue::Entity(run.Entity(row)));
                const AuthoredQueryStatus status = dispatcher->Evaluate(bound.Query, arguments.Values(), answer);
                statuses[row] = FromQuery(status);
                if (statuses[row] == CandidateMeasureStatus::Stale)
                    run.MarkDefinitionStale();
                values[row] = status == AuthoredQueryStatus::Value ? NumericValue(answer) : 0.0f;
            }
        }
    }

    CandidateMeasureDefinition AuthoredQueryMeasure()
    {
        CandidateMeasureDefinition definition;
        definition.Name = "candidates.measure.authored_query";
        definition.DisplayName = "Authored query";
        definition.Description = "Asks a game-declared authored query about each entity candidate. The engine's "
                                 "side of the call allocates nothing with non-string constants; the game's "
                                 "implementation is its own.";
        DataFieldSchema constants = MakeDataField(DataFieldKind::Record, "arguments", "Constant arguments");
        constants.Required = false;
        definition.Arguments = Record({ MakeDataField(DataFieldKind::String, "query", "Query"),
                                        MakeDataField(DataFieldKind::String, "entity_argument", "Entity argument",
                                                      "The query's Entity argument that receives the candidate."),
                                        std::move(constants) });
        definition.OpenArguments = true;
        definition.Cost = CandidateCostClass::Entity;
        definition.AppliesTo = CandidateAppliesTo::Entity;
        definition.Value = CandidateValueKind::Scalar;
        definition.Prepare = PrepareAuthoredQuery;
        definition.Bind = BindAuthoredQuery;
        definition.Measure = MeasureAuthoredQuery;
        return definition;
    }
}
