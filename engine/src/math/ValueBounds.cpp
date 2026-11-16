#include <math/ValueBounds.h>

#include <core/metadata/DataSchemaFields.h>

#include <format>
#include <utility>

bool ReadValueBounds(const JsonValue& require, ValueBounds& out, std::string_view subject,
                     std::vector<std::string>& errors)
{
    if (!require.IsObject())
    {
        errors.push_back(std::format("{}: 'require' is {{min, max}} or {{expect}}", subject));
        return false;
    }
    if (const JsonValue* expect = require.Find("expect"))
    {
        if (!expect->IsBool() || require.Find("min") != nullptr || require.Find("max") != nullptr)
        {
            errors.push_back(std::format("{}: require.expect is true or false, and stands alone", subject));
            return false;
        }
        out = ValueBounds::Exactly(expect->AsBool() ? 1.0 : 0.0);
        return true;
    }
    out.Min = require.NumberOr("min", out.Min);
    out.Max = require.NumberOr("max", out.Max);
    if (out.Min > out.Max)
    {
        errors.push_back(std::format("{}: require.min exceeds require.max", subject));
        return false;
    }
    return true;
}

DataFieldSchema MakeValueBoundsField(std::string key, std::string display)
{
    DataFieldSchema require = DataField::Field(std::move(key), DataFieldKind::Record, std::move(display), {}, false);
    require.Children.push_back(DataField::Field("min", DataFieldKind::Float, "Minimum", {}, false));
    require.Children.push_back(DataField::Field("max", DataFieldKind::Float, "Maximum", {}, false));
    require.Children.push_back(DataField::Field("expect", DataFieldKind::Bool, "Expect", {}, false));
    return require;
}
