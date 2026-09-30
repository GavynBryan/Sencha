#include "authoring/UiPreviewModel.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <ui/UiService.h>

#include <fstream>

namespace
{
    JsonValue ValueToJson(const UiValue& value, std::string* error)
    {
        JsonValue::Object out;
        switch (value.Kind())
        {
        case UiValueKind::Bool:
            out.emplace_back("kind", JsonValue("bool"));
            out.emplace_back("value", JsonValue(value.AsBool()));
            break;
        case UiValueKind::Int:
            out.emplace_back("kind", JsonValue("int"));
            out.emplace_back("value", JsonValue(static_cast<double>(value.AsInt())));
            break;
        case UiValueKind::Float:
            out.emplace_back("kind", JsonValue("float"));
            out.emplace_back("value", JsonValue(value.AsFloat()));
            break;
        case UiValueKind::String:
            out.emplace_back("kind", JsonValue("string"));
            out.emplace_back("value", JsonValue(std::string(value.AsString())));
            break;
        case UiValueKind::Id:
            if (error) *error = "an identity is minted by a host and cannot be a sample value";
            break;
        case UiValueKind::None:
        default:
            out.emplace_back("kind", JsonValue("none"));
            break;
        }
        return JsonValue(std::move(out));
    }

    std::optional<UiValue> ValueFromJson(const JsonValue& json, std::string* error)
    {
        const JsonValue* kind = json.Find("kind");
        const JsonValue* value = json.Find("value");
        if (kind == nullptr || !kind->IsString())
        {
            if (error) *error = "a value needs a \"kind\"";
            return std::nullopt;
        }
        const std::string& k = kind->AsString();
        if (k == "none")
            return UiValue{};
        if (value == nullptr)
        {
            if (error) *error = "a value of kind '" + k + "' needs a \"value\"";
            return std::nullopt;
        }
        if (k == "bool" && value->IsBool()) return UiValue(value->AsBool());
        if (k == "int" && value->IsNumber()) return UiValue(static_cast<std::int64_t>(value->AsNumber()));
        if (k == "float" && value->IsNumber()) return UiValue(value->AsNumber());
        if (k == "string" && value->IsString()) return UiValue(value->AsString());
        if (error) *error = "value does not match its kind '" + k + "'";
        return std::nullopt;
    }

    JsonValue RowToJson(const UiRow& row)
    {
        JsonValue::Object out;
        out.emplace_back("label", JsonValue(row.Label));
        out.emplace_back("value", JsonValue(row.Value));
        if (!row.Detail.empty())
            out.emplace_back("detail", JsonValue(row.Detail));
        out.emplace_back("editable", JsonValue(row.Editable));
        if (row.Control != UiRowControl::Text)
            out.emplace_back("control", JsonValue(UiRowControlName(row.Control)));
        if (row.Control == UiRowControl::Range)
        {
            out.emplace_back("number", JsonValue(row.Number));
            out.emplace_back("min", JsonValue(row.Min));
            out.emplace_back("max", JsonValue(row.Max));
            out.emplace_back("step", JsonValue(row.Step));
        }
        if (row.Control == UiRowControl::Choice)
        {
            JsonValue::Array choices;
            for (const std::string& c : row.Choices)
                choices.emplace_back(c);
            out.emplace_back("choices", JsonValue(std::move(choices)));
        }
        return JsonValue(std::move(out));
    }

    std::optional<UiRow> RowFromJson(const JsonValue& json, std::string* error)
    {
        if (!json.IsObject())
        {
            if (error) *error = "a row must be an object";
            return std::nullopt;
        }
        UiRow row;
        if (const JsonValue* v = json.Find("label"); v && v->IsString()) row.Label = v->AsString();
        if (const JsonValue* v = json.Find("value"); v && v->IsString()) row.Value = v->AsString();
        if (const JsonValue* v = json.Find("detail"); v && v->IsString()) row.Detail = v->AsString();
        if (const JsonValue* v = json.Find("editable"); v && v->IsBool()) row.Editable = v->AsBool();
        if (const JsonValue* v = json.Find("control"); v && v->IsString())
        {
            const std::optional<UiRowControl> control = ParseUiRowControl(v->AsString());
            if (!control)
            {
                if (error) *error = "unknown row control '" + v->AsString() + "'";
                return std::nullopt;
            }
            row.Control = *control;
        }
        if (const JsonValue* v = json.Find("number"); v && v->IsNumber()) row.Number = v->AsNumber();
        if (const JsonValue* v = json.Find("min"); v && v->IsNumber()) row.Min = v->AsNumber();
        if (const JsonValue* v = json.Find("max"); v && v->IsNumber()) row.Max = v->AsNumber();
        if (const JsonValue* v = json.Find("step"); v && v->IsNumber()) row.Step = v->AsNumber();
        if (const JsonValue* v = json.Find("choices"); v && v->IsArray())
            for (const JsonValue& c : v->AsArray())
                if (c.IsString())
                    row.Choices.push_back(c.AsString());
        return row;
    }

    std::vector<std::string> StringList(const JsonValue* json)
    {
        std::vector<std::string> out;
        if (json == nullptr || !json->IsArray())
            return out;
        for (const JsonValue& item : json->AsArray())
            if (item.IsString())
                out.push_back(item.AsString());
        return out;
    }
}

const char* UiRowControlName(UiRowControl control)
{
    switch (control)
    {
    case UiRowControl::Range:  return "range";
    case UiRowControl::Choice: return "choice";
    case UiRowControl::Text:
    default:                   return "text";
    }
}

std::optional<UiRowControl> ParseUiRowControl(std::string_view name)
{
    if (name == "text") return UiRowControl::Text;
    if (name == "range") return UiRowControl::Range;
    if (name == "choice") return UiRowControl::Choice;
    return std::nullopt;
}

std::filesystem::path UiPreviewModel::SidecarFor(const std::filesystem::path& documentSource)
{
    std::filesystem::path sidecar = documentSource;
    sidecar.replace_extension(".preview.json");
    return sidecar;
}

std::optional<UiPreviewModel> UiPreviewModel::Parse(const JsonValue& json, std::string* error)
{
    if (!json.IsObject())
    {
        if (error) *error = "preview model: root must be an object";
        return std::nullopt;
    }
    UiPreviewModel model;
    if (const JsonValue* v = json.Find("model"); v && v->IsString())
        model.ModelName = v->AsString();
    if (const JsonValue* v = json.Find("modal"); v && v->IsBool())
        model.Modal = v->AsBool();

    if (const JsonValue* props = json.Find("properties"); props && props->IsArray())
    {
        for (const JsonValue& p : props->AsArray())
        {
            const JsonValue* name = p.Find("name");
            if (name == nullptr || !name->IsString())
            {
                if (error) *error = "preview model: a property needs a \"name\"";
                return std::nullopt;
            }
            std::string valueError;
            const std::optional<UiValue> value = ValueFromJson(p, &valueError);
            if (!value)
            {
                if (error) *error = "preview model: property '" + name->AsString() + "': " + valueError;
                return std::nullopt;
            }
            bool editable = false;
            if (const JsonValue* e = p.Find("editable"); e && e->IsBool())
                editable = e->AsBool();
            model.Properties.push_back(UiModelProperty{ name->AsString(), *value, editable });
        }
    }
    if (const JsonValue* arrays = json.Find("arrays"); arrays && arrays->IsArray())
    {
        for (const JsonValue& a : arrays->AsArray())
        {
            const JsonValue* name = a.Find("name");
            if (name == nullptr || !name->IsString())
            {
                if (error) *error = "preview model: an array needs a \"name\"";
                return std::nullopt;
            }
            model.Arrays.push_back({ name->AsString(), StringList(a.Find("items")) });
        }
    }
    if (const JsonValue* rows = json.Find("rows"); rows && rows->IsArray())
    {
        for (const JsonValue& r : rows->AsArray())
        {
            const JsonValue* name = r.Find("name");
            if (name == nullptr || !name->IsString())
            {
                if (error) *error = "preview model: a row list needs a \"name\"";
                return std::nullopt;
            }
            RowsSample sample{ name->AsString(), {} };
            if (const JsonValue* items = r.Find("items"); items && items->IsArray())
            {
                for (const JsonValue& item : items->AsArray())
                {
                    std::string rowError;
                    const std::optional<UiRow> row = RowFromJson(item, &rowError);
                    if (!row)
                    {
                        if (error) *error = "preview model: rows '" + sample.Name + "': " + rowError;
                        return std::nullopt;
                    }
                    sample.Items.push_back(*row);
                }
            }
            model.Rows.push_back(std::move(sample));
        }
    }
    model.Actions = StringList(json.Find("actions"));
    return model;
}

std::optional<UiPreviewModel> UiPreviewModel::Load(const std::filesystem::path& sidecar, std::string* error)
{
    const std::optional<JsonValue> json = JsonParseFile(sidecar, error);
    if (!json)
        return std::nullopt;
    return Parse(*json, error);
}

JsonValue UiPreviewModel::ToJson() const
{
    JsonValue::Object out;
    out.emplace_back("model", JsonValue(ModelName));
    out.emplace_back("modal", JsonValue(Modal));

    JsonValue::Array properties;
    for (const UiModelProperty& p : Properties)
    {
        JsonValue value = ValueToJson(p.Initial, nullptr);
        JsonValue::Object& fields = value.AsObject();
        fields.insert(fields.begin(), { "name", JsonValue(p.Path) });
        if (p.Editable)
            fields.emplace_back("editable", JsonValue(true));
        properties.push_back(std::move(value));
    }
    out.emplace_back("properties", JsonValue(std::move(properties)));

    JsonValue::Array arrays;
    for (const ArraySample& a : Arrays)
    {
        JsonValue::Array items;
        for (const std::string& s : a.Items)
            items.emplace_back(s);
        arrays.push_back(JsonValue(JsonValue::Object{ { "name", JsonValue(a.Name) },
                                                      { "items", JsonValue(std::move(items)) } }));
    }
    out.emplace_back("arrays", JsonValue(std::move(arrays)));

    JsonValue::Array rows;
    for (const RowsSample& r : Rows)
    {
        JsonValue::Array items;
        for (const UiRow& row : r.Items)
            items.push_back(RowToJson(row));
        rows.push_back(JsonValue(JsonValue::Object{ { "name", JsonValue(r.Name) },
                                                    { "items", JsonValue(std::move(items)) } }));
    }
    out.emplace_back("rows", JsonValue(std::move(rows)));

    JsonValue::Array actions;
    for (const std::string& a : Actions)
        actions.emplace_back(a);
    out.emplace_back("actions", JsonValue(std::move(actions)));
    return JsonValue(std::move(out));
}

bool UiPreviewModel::Save(const std::filesystem::path& sidecar, std::string* error) const
{
    for (const UiModelProperty& p : Properties)
    {
        std::string valueError;
        (void)ValueToJson(p.Initial, &valueError);
        if (!valueError.empty())
        {
            if (error) *error = "property '" + p.Path + "': " + valueError;
            return false;
        }
    }
    std::ofstream out(sidecar, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        if (error) *error = "could not write '" + sidecar.string() + "'";
        return false;
    }
    out << JsonStringify(ToJson(), true) << '\n';
    return static_cast<bool>(out);
}

UiScreenDesc UiPreviewModel::Describe(std::string packagePath) const
{
    UiScreenDesc desc;
    desc.PackagePath = std::move(packagePath);
    desc.ModelName = ModelName;
    desc.Modal = Modal;
    desc.Properties = Properties;
    for (const ArraySample& a : Arrays)
        desc.Arrays.push_back(a.Name);
    for (const RowsSample& r : Rows)
        desc.RowLists.push_back(r.Name);
    desc.Actions = Actions;
    return desc;
}

void UiPreviewModel::Publish(UiService& ui, UiScreenHandle screen) const
{
    for (std::size_t i = 0; i < Arrays.size(); ++i)
        (void)ui.SetArray(screen, UiArrayIdAt(i), Arrays[i].Items);
    for (std::size_t i = 0; i < Rows.size(); ++i)
        (void)ui.SetRows(screen, UiRowsIdAt(i), Rows[i].Items);
}

bool UiPreviewModel::DeclaresProperty(std::string_view name) const
{
    for (const UiModelProperty& p : Properties)
        if (p.Path == name)
            return true;
    return false;
}

bool UiPreviewModel::DeclaresArray(std::string_view name) const
{
    for (const ArraySample& a : Arrays)
        if (a.Name == name)
            return true;
    return false;
}

bool UiPreviewModel::DeclaresRows(std::string_view name) const
{
    for (const RowsSample& r : Rows)
        if (r.Name == name)
            return true;
    return false;
}

bool UiPreviewModel::DeclaresAction(std::string_view name) const
{
    for (const std::string& a : Actions)
        if (a == name)
            return true;
    return false;
}
