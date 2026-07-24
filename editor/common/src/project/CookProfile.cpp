#include "CookProfile.h"

#include <algorithm>
#include <array>
#include <unordered_set>

namespace
{
    constexpr const char* DispositionName(CookOutputDisposition disposition)
    {
        switch (disposition)
        {
        case CookOutputDisposition::Publish: return "publish";
        case CookOutputDisposition::Preserve: return "preserve";
        case CookOutputDisposition::Withdraw: return "withdraw";
        }
        return "preserve";
    }

    bool ParseDisposition(std::string_view name, CookOutputDisposition& out)
    {
        if (name == "publish")
            out = CookOutputDisposition::Publish;
        else if (name == "preserve")
            out = CookOutputDisposition::Preserve;
        else if (name == "withdraw")
            out = CookOutputDisposition::Withdraw;
        else
            return false;
        return true;
    }

    CookOutputPolicy Policy(std::string_view family, CookOutputDisposition disposition)
    {
        return CookOutputPolicy{ std::string(family), disposition };
    }

    CookProfile FullProfile()
    {
        return CookProfile{
            .Id = "full",
            .Name = "Full",
            .TargetSteps = {
                std::string(CookStepIds::RenderMeshes),
                std::string(CookStepIds::Collision),
                std::string(CookStepIds::ReferencedAssets),
                std::string(CookStepIds::DirectLightmap),
                std::string(CookStepIds::AmbientOcclusion),
                std::string(CookStepIds::IrradianceProbes),
            },
            .OutputPolicies = {
                Policy(CookOutputFamilies::Structure, CookOutputDisposition::Publish),
                Policy(CookOutputFamilies::Collision, CookOutputDisposition::Publish),
                Policy(CookOutputFamilies::ReferencedAssets, CookOutputDisposition::Publish),
                Policy(CookOutputFamilies::DirectLightmap, CookOutputDisposition::Publish),
                Policy(CookOutputFamilies::AmbientOcclusion, CookOutputDisposition::Publish),
                Policy(CookOutputFamilies::IrradianceProbes, CookOutputDisposition::Publish),
            },
            .BuiltIn = true,
        };
    }

    CookProfile LightingOnlyProfile()
    {
        return CookProfile{
            .Id = "lighting_only",
            .Name = "Lighting Only",
            .TargetSteps = {
                std::string(CookStepIds::DirectLightmap),
                std::string(CookStepIds::AmbientOcclusion),
                std::string(CookStepIds::IrradianceProbes),
            },
            .OutputPolicies = {
                Policy(CookOutputFamilies::Structure, CookOutputDisposition::Preserve),
                Policy(CookOutputFamilies::Collision, CookOutputDisposition::Preserve),
                Policy(CookOutputFamilies::ReferencedAssets, CookOutputDisposition::Preserve),
                Policy(CookOutputFamilies::DirectLightmap, CookOutputDisposition::Publish),
                Policy(CookOutputFamilies::AmbientOcclusion, CookOutputDisposition::Publish),
                Policy(CookOutputFamilies::IrradianceProbes, CookOutputDisposition::Publish),
            },
            .BuiltIn = true,
        };
    }

    CookProfile NoLightingProfile()
    {
        return CookProfile{
            .Id = "no_lighting",
            .Name = "No Lighting",
            .TargetSteps = {
                std::string(CookStepIds::RenderMeshes),
                std::string(CookStepIds::Collision),
                std::string(CookStepIds::ReferencedAssets),
            },
            .OutputPolicies = {
                Policy(CookOutputFamilies::Structure, CookOutputDisposition::Publish),
                Policy(CookOutputFamilies::Collision, CookOutputDisposition::Publish),
                Policy(CookOutputFamilies::ReferencedAssets, CookOutputDisposition::Publish),
                Policy(CookOutputFamilies::DirectLightmap, CookOutputDisposition::Withdraw),
                Policy(CookOutputFamilies::AmbientOcclusion, CookOutputDisposition::Withdraw),
                Policy(CookOutputFamilies::IrradianceProbes, CookOutputDisposition::Withdraw),
            },
            .BuiltIn = true,
        };
    }
}

std::span<const CookProfile> BuiltInCookProfiles()
{
    static const std::array<CookProfile, 3> profiles{
        FullProfile(), LightingOnlyProfile(), NoLightingProfile()
    };
    return profiles;
}

std::vector<CookProfile> ResolveCookProfiles(std::span<const CookProfile> projectProfiles)
{
    const std::span<const CookProfile> builtIns = BuiltInCookProfiles();
    std::vector<CookProfile> result(builtIns.begin(), builtIns.end());
    result.reserve(result.size() + projectProfiles.size());
    for (const CookProfile& source : projectProfiles)
    {
        CookProfile profile = source;
        profile.BuiltIn = false;
        result.push_back(std::move(profile));
    }
    return result;
}

JsonValue WriteCookProfiles(std::span<const CookProfile> profiles)
{
    JsonValue::Array array;
    for (const CookProfile& profile : profiles)
    {
        if (profile.BuiltIn)
            continue;

        JsonValue::Array steps;
        for (const std::string& step : profile.TargetSteps)
            steps.push_back(JsonValue(step));

        JsonValue::Object outputs;
        for (const CookOutputPolicy& policy : profile.OutputPolicies)
            outputs.emplace_back(policy.Family, JsonValue(DispositionName(policy.Disposition)));

        array.push_back(JsonValue(JsonValue::Object{
            { "id", JsonValue(profile.Id) },
            { "name", JsonValue(profile.Name) },
            { "steps", JsonValue(std::move(steps)) },
            { "outputs", JsonValue(std::move(outputs)) },
        }));
    }
    return JsonValue(std::move(array));
}

bool ReadCookProfiles(const JsonValue& value,
                      std::vector<CookProfile>& out,
                      std::string* error)
{
    const auto fail = [error](std::string message)
    {
        if (error != nullptr)
            *error = std::move(message);
        return false;
    };

    if (!value.IsArray())
        return fail("cookProfiles must be an array");

    std::unordered_set<std::string> profileIds;
    for (const CookProfile& builtIn : BuiltInCookProfiles())
        profileIds.insert(builtIn.Id);

    std::vector<CookProfile> profiles;
    for (const JsonValue& entry : value.AsArray())
    {
        if (!entry.IsObject())
            return fail("cook profile must be an object");
        const JsonValue* id = entry.Find("id");
        const JsonValue* name = entry.Find("name");
        const JsonValue* steps = entry.Find("steps");
        const JsonValue* outputs = entry.Find("outputs");
        if (id == nullptr || !id->IsString() || id->AsString().empty())
            return fail("cook profile requires a non-empty string id");
        if (name == nullptr || !name->IsString() || name->AsString().empty())
            return fail("cook profile requires a non-empty string name");
        if (steps == nullptr || !steps->IsArray())
            return fail("cook profile requires a steps array");
        if (outputs == nullptr || !outputs->IsObject())
            return fail("cook profile requires an outputs object");
        if (!profileIds.insert(id->AsString()).second)
            return fail("duplicate cook profile id '" + id->AsString() + "'");

        CookProfile profile;
        profile.Id = id->AsString();
        profile.Name = name->AsString();
        std::unordered_set<std::string> stepIds;
        for (const JsonValue& step : steps->AsArray())
        {
            if (!step.IsString())
                return fail("cook profile step ids must be strings");
            if (!stepIds.insert(step.AsString()).second)
                return fail("duplicate cook profile step '" + step.AsString() + "'");
            profile.TargetSteps.push_back(step.AsString());
        }
        std::unordered_set<std::string> outputFamilies;
        for (const auto& [family, dispositionValue] : outputs->AsObject())
        {
            if (!dispositionValue.IsString())
                return fail("cook output dispositions must be strings");
            if (!outputFamilies.insert(family).second)
                return fail("duplicate cook output family '" + family + "'");
            CookOutputDisposition disposition;
            if (!ParseDisposition(dispositionValue.AsString(), disposition))
                return fail("unknown cook output disposition '" + dispositionValue.AsString() + "'");
            profile.OutputPolicies.push_back(CookOutputPolicy{ family, disposition });
        }
        profiles.push_back(std::move(profile));
    }

    out = std::move(profiles);
    return true;
}
