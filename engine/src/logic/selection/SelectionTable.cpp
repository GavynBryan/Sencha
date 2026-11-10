#include <logic/selection/SelectionTable.h>

#include <assets/data/DataAssetCache.h>
#include <authored/AuthoredQueryRegistry.h>
#include <gameplay_tags/GameplayTagQueryNames.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <format>

namespace
{
    template<typename Names>
    std::optional<std::uint8_t> IndexIn(const Names& names, std::string_view name)
    {
        const auto found = std::ranges::find(names, name);
        if (found == names.end())
            return std::nullopt;
        return static_cast<std::uint8_t>(found - names.begin());
    }

    [[nodiscard]] bool IsNumeric(DataFieldKind kind)
    {
        return kind == DataFieldKind::Bool || kind == DataFieldKind::Int || kind == DataFieldKind::Float;
    }

    bool BindQuery(const SelectionConsiderationDesc& desc, const AuthoredQueryRegistry* queries,
                   SelectionConsideration& out, std::string_view subject, std::vector<std::string>& errors)
    {
        if (queries == nullptr)
        {
            errors.push_back(std::format("{}: no authored query catalog to resolve '{}' against", subject, desc.Query));
            return false;
        }
        std::vector<std::string_view> arguments;
        for (const SelectionEntityArgument& argument : desc.EntityArguments)
        {
            arguments.push_back(argument.Argument);
            out.Entities.push_back(argument.Entity);
        }
        out.EntitySlots.resize(arguments.size());
        bool ok = CompileAuthoredQueryCall(*queries, desc.Query, desc.Constants, arguments, out.EntitySlots, subject,
                                           out.Call, errors);
        if (!out.Call.Query.IsValid())
            return false;
        const DataFieldKind result = queries->Get(out.Call.Query.Slot)->Result.Kind;
        if (!IsNumeric(result))
        {
            errors.push_back(std::format("{}: '{}' answers with a value no curve can score", subject, desc.Query));
            ok = false;
        }
        else if (desc.Rule.Mode == ConsiderationMode::Score && !desc.Rule.Curve && result != DataFieldKind::Bool)
        {
            errors.push_back(std::format("{}: scoring '{}' needs a curve", subject, desc.Query));
            ok = false;
        }
        return ok;
    }
}

std::optional<std::uint8_t> SelectionTable::FindEntity(std::string_view name) const
{
    return IndexIn(Entities_, name);
}

std::optional<std::uint8_t> SelectionTable::FindInput(std::string_view name) const
{
    return IndexIn(Inputs_, name);
}

std::optional<std::uint32_t> SelectionTable::FindOption(std::string_view name) const
{
    const auto found = std::ranges::find(Options_, name, &SelectionOption::Name);
    if (found == Options_.end())
        return std::nullopt;
    return static_cast<std::uint32_t>(found - Options_.begin());
}

bool BindSelectionTable(const SelectionTableDesc& desc, const AuthoredQueryRegistry* queries,
                        const GameplayTagRegistry* tags, SelectionTable& out, std::vector<std::string>& errors)
{
    SelectionTable bound;
    bound.Name_ = desc.Name;
    bound.Entities_ = desc.Entities;
    bound.Inputs_ = desc.Inputs;
    bound.SwitchMargin_ = desc.SwitchMargin;
    bound.Mode_ = desc.Mode;
    bound.BandWidth_ = desc.BandWidth;

    bool ok = true;
    if (!desc.Interrupt.Empty())
    {
        if (tags == nullptr)
        {
            errors.push_back("interrupt: no tag registry to resolve tags against");
            ok = false;
        }
        else
        {
            std::vector<std::string> missing;
            bound.Interrupt_ = ResolveTagQuery(desc.Interrupt, *tags, missing);
            bound.HasInterrupt = true;
            for (const std::string& name : missing)
            {
                errors.push_back(std::format("interrupt: '{}' is not a registered gameplay tag", name));
                ok = false;
            }
        }
    }

    for (std::size_t optionIndex = 0; optionIndex < desc.Options.size(); ++optionIndex)
    {
        const SelectionOptionDesc& option = desc.Options[optionIndex];
        SelectionOption boundOption{ option.Name, option.CommitSeconds, {} };
        for (std::size_t index = 0; index < option.Considerations.size(); ++index)
        {
            const SelectionConsiderationDesc& consideration = option.Considerations[index];
            SelectionConsideration boundConsideration;
            boundConsideration.Source = consideration.Source;
            boundConsideration.Input = consideration.Input;
            boundConsideration.Rule = consideration.Rule;
            if (consideration.Source == SelectionSource::Query)
            {
                const std::string subject = std::format("option {} '{}' consideration {}", optionIndex, option.Name, index);
                ok = BindQuery(consideration, queries, boundConsideration, subject, errors) && ok;
            }
            boundOption.Considerations.push_back(std::move(boundConsideration));
        }
        bound.Options_.push_back(std::move(boundOption));
    }

    if (!ok)
        return false;
    out = std::move(bound);
    return true;
}

bool SelectionTableBinding::BindFrom(const DataAssetCache& cache, DataAssetHandle asset,
                                     const AuthoredQueryRegistry* queries, const GameplayTagRegistry* tags,
                                     std::vector<std::string>& errors)
{
    Asset = DataAssetStamp::Of(cache, asset);
    Queries = queries != nullptr ? CatalogStamp::Of(*queries) : CatalogStamp{};
    Tags = tags != nullptr ? TagVocabularyStamp::Of(*tags) : TagVocabularyStamp{};
    Bound.reset();

    const auto* desc = cache.TryGet<SelectionTableDesc>(asset, kSelectionTableTypeName);
    if (desc == nullptr)
    {
        errors.push_back(std::format("'{}' is not a resident selection table", cache.GetName(asset)));
        return false;
    }
    SelectionTable table;
    if (!BindSelectionTable(*desc, queries, tags, table, errors))
        return false;
    Bound = std::move(table);
    return true;
}

bool SelectionTableBinding::Refresh(const DataAssetCache& cache, const AuthoredQueryRegistry* queries,
                                    const GameplayTagRegistry* tags, std::vector<std::string>& errors)
{
    bool changed = !Asset.Matches(cache);
    if (queries != nullptr)
        changed = changed || !Queries.Matches(*queries);
    if (tags != nullptr)
        changed = changed || !Tags.Matches(*tags);
    if (!changed)
        return false;
    (void)BindFrom(cache, Asset.Asset, queries, tags, errors);
    return true;
}
