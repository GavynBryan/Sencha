#include <assets/cook/NavigationSettings.h>

#include <core/metadata/DataSchema.h>

#include <set>

namespace
{
    DataFieldSchema NumberField(std::string key, std::string display, double fallback,
                                std::string units)
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::Float, std::move(key), std::move(display));
        field.Units = std::move(units);
        field.Default = fallback;
        field.Required = false;
        return field;
    }

    // Types and shape only. Build-profile ranges belong to IsValidNavBuildProfile,
    // which the runtime also relies on.
    DataSchema MakeNavigationSettingsSchema()
    {
        const NavBuildProfile defaults;
        DataFieldSchema tileCells = MakeDataField(DataFieldKind::Int, "tile_cells", "Tile cells",
                                                  "Tile edge length in voxels.");
        tileCells.Default = static_cast<std::int64_t>(defaults.TileCells);
        tileCells.Numeric.Minimum = kNavMinTileCells;
        tileCells.Numeric.Maximum = kNavMaxTileCells;
        tileCells.Required = false;

        DataFieldSchema profile = MakeDataField(DataFieldKind::Record, {}, "Build profile");
        profile.Children = {
            MakeDataField(DataFieldKind::GameplayTag, "tag", "Profile"),
            NumberField("radius", "Radius", defaults.Radius, "m"),
            NumberField("height", "Height", defaults.Height, "m"),
            NumberField("max_slope_degrees", "Max slope", defaults.MaxSlopeDegrees, "deg"),
            NumberField("max_climb", "Max climb", defaults.MaxClimb, "m"),
            NumberField("cell_size", "Cell size", defaults.CellSize, "m"),
            NumberField("cell_height", "Cell height", defaults.CellHeight, "m"),
            std::move(tileCells),
        };
        DataFieldSchema profiles = MakeDataField(DataFieldKind::Array, "profiles", "Profiles",
                                                 "Agent shapes to build navigation for.");
        profiles.Children.push_back(std::move(profile));

        DataFieldSchema areas = MakeDataField(DataFieldKind::Array, "areas", "Areas",
                                              "Ground areas beyond the default, in index order.");
        areas.Required = false;
        areas.Children.push_back(MakeDataField(DataFieldKind::GameplayTag, {}, "Area"));

        DataSchema schema;
        schema.TypeName = std::string(kNavigationSettingsSubtype);
        schema.DisplayName = "Navigation Settings";
        schema.Root = MakeDataField(DataFieldKind::Record, {}, "Navigation settings");
        schema.Root.Children = { std::move(profiles), std::move(areas) };
        return schema;
    }

    void Report(std::vector<CookDiagnostic>& diagnostics, std::string message)
    {
        diagnostics.push_back(CookDiagnostic{
            .Severity = CookDiagnosticSeverity::Error,
            .Source = CookDiagnosticSource::NavigationSettings,
            .Rule = "nav.settings.invalid",
            .Message = std::move(message),
        });
    }

    NavigationProfileSetting ReadProfile(const JsonValue& entry)
    {
        NavigationProfileSetting profile{ entry.Find("tag")->AsString(), {} };
        NavBuildProfile& build = profile.Build;
        const auto read = [&entry](std::string_view key, float& value)
        { value = static_cast<float>(entry.NumberOr(key, value)); };
        read("radius", build.Radius);
        read("height", build.Height);
        read("max_slope_degrees", build.MaxSlopeDegrees);
        read("max_climb", build.MaxClimb);
        read("cell_size", build.CellSize);
        read("cell_height", build.CellHeight);
        // The schema bounds tile_cells, so the conversion is exact.
        build.TileCells = static_cast<std::uint32_t>(entry.NumberOr("tile_cells", build.TileCells));
        return profile;
    }

    void ReadProfiles(const JsonValue& list, NavigationSettings& settings,
                      std::vector<CookDiagnostic>& diagnostics)
    {
        if (list.AsArray().empty())
            Report(diagnostics, "navigation settings list no profiles");
        std::set<std::string> names;
        for (const JsonValue& entry : list.AsArray())
        {
            NavigationProfileSetting profile = ReadProfile(entry);
            if (!IsValidNavBuildProfile(profile.Build))
                Report(diagnostics, "navigation profile '" + profile.Name + "' has an out-of-range value");
            else if (!names.insert(profile.Name).second)
                Report(diagnostics, "navigation profile '" + profile.Name + "' is listed twice");
            else
                settings.Profiles.push_back(std::move(profile));
        }
    }

    void ReadAreas(const JsonValue* list, NavigationSettings& settings,
                   std::vector<CookDiagnostic>& diagnostics)
    {
        if (list == nullptr)
            return;
        std::set<std::string> names{ std::string(kNavigationDefaultAreaName) };
        for (const JsonValue& entry : list->AsArray())
        {
            if (!names.insert(entry.AsString()).second)
                Report(diagnostics, "navigation area '" + entry.AsString()
                                        + "' is listed twice or names the default area");
            else
                settings.Areas.push_back(entry.AsString());
        }
        if (settings.Areas.size() > kNavMaxAuthoredAreas)
            Report(diagnostics, "navigation settings list more than 62 areas");
    }
}

const DataSchema& NavigationSettingsSchema()
{
    static const DataSchema schema = MakeNavigationSettingsSchema();
    return schema;
}

bool ParseNavigationSettings(const JsonValue& envelope, NavigationSettings& settings,
                             std::vector<CookDiagnostic>& diagnostics)
{
    settings = NavigationSettings{};
    const std::size_t reportedBefore = diagnostics.size();
    const JsonValue* type = envelope.Find("type");
    const JsonValue* data = envelope.Find("data");
    if (type == nullptr || !type->IsString() || type->AsString() != kNavigationSettingsSubtype
        || data == nullptr)
    {
        Report(diagnostics, "not a navigation.settings data asset");
        return false;
    }
    std::vector<DataValidationError> errors;
    if (!ValidateDataAgainstSchema(*data, NavigationSettingsSchema(), errors))
    {
        for (const DataValidationError& error : errors)
            Report(diagnostics, error.Path + ": " + error.Message);
        return false;
    }
    ReadProfiles(*data->Find("profiles"), settings, diagnostics);
    ReadAreas(data->Find("areas"), settings, diagnostics);
    return diagnostics.size() == reportedBefore;
}
