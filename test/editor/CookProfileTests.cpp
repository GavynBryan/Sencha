#include <project/CookProfile.h>
#include <project/Project.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>

namespace
{
    const CookProfile* FindProfile(std::span<const CookProfile> profiles,
                                   std::string_view id)
    {
        const auto found = std::find_if(profiles.begin(), profiles.end(),
            [id](const CookProfile& profile) { return profile.Id == id; });
        return found != profiles.end() ? &*found : nullptr;
    }

    CookOutputDisposition Disposition(const CookProfile& profile,
                                      std::string_view family)
    {
        const auto found = std::find_if(profile.OutputPolicies.begin(),
            profile.OutputPolicies.end(),
            [family](const CookOutputPolicy& policy) { return policy.Family == family; });
        return found != profile.OutputPolicies.end()
            ? found->Disposition
            : CookOutputDisposition::Preserve;
    }
}

TEST(CookProfileTest, BuiltInsDescribeWorkAndPublicationSeparately)
{
    const std::span<const CookProfile> profiles = BuiltInCookProfiles();
    ASSERT_EQ(profiles.size(), 3u);

    const CookProfile* full = FindProfile(profiles, "full");
    const CookProfile* lighting = FindProfile(profiles, "lighting_only");
    const CookProfile* noLighting = FindProfile(profiles, "no_lighting");
    ASSERT_NE(full, nullptr);
    ASSERT_NE(lighting, nullptr);
    ASSERT_NE(noLighting, nullptr);

    EXPECT_TRUE(full->BuiltIn);
    EXPECT_NE(std::find(full->TargetSteps.begin(), full->TargetSteps.end(),
                        CookStepIds::Collision), full->TargetSteps.end());
    EXPECT_EQ(Disposition(*lighting, CookOutputFamilies::Structure),
              CookOutputDisposition::Preserve);
    EXPECT_EQ(Disposition(*lighting, CookOutputFamilies::DirectLightmap),
              CookOutputDisposition::Publish);
    EXPECT_EQ(Disposition(*noLighting, CookOutputFamilies::DirectLightmap),
              CookOutputDisposition::Withdraw);
    EXPECT_EQ(Disposition(*noLighting, CookOutputFamilies::IrradianceProbes),
              CookOutputDisposition::Withdraw);
}

TEST(CookProfileTest, ProjectProfilesRoundTripWithoutSerializingBuiltIns)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path()
        / ("sencha_cook_profiles_" + std::to_string(
            reinterpret_cast<std::uintptr_t>(::testing::UnitTest::GetInstance())));
    fs::remove_all(root);
    fs::create_directories(root / "assets");

    ProjectDescriptor source;
    source.Name = "Profiles";
    source.GameModulePath = (root / "build/game.so").string();
    source.ContentRoots = { (root / "assets").string() };
    source.CookProfiles.push_back(CookProfile{
        .Id = "fast_geometry",
        .Name = "Fast Geometry",
        .TargetSteps = { std::string(CookStepIds::RenderMeshes) },
        .OutputPolicies = {
            CookOutputPolicy{ std::string(CookOutputFamilies::Structure),
                              CookOutputDisposition::Publish },
            CookOutputPolicy{ std::string(CookOutputFamilies::DirectLightmap),
                              CookOutputDisposition::Withdraw },
        },
    });

    const fs::path path = root / "project.senchaproj";
    std::string error;
    ASSERT_TRUE(source.Save(path.string(), &error)) << error;

    ProjectDescriptor loaded;
    ASSERT_TRUE(ProjectDescriptor::Load(path.string(), loaded, &error)) << error;
    ASSERT_EQ(loaded.CookProfiles.size(), 1u);
    EXPECT_EQ(loaded.CookProfiles.front().Id, "fast_geometry");
    EXPECT_FALSE(loaded.CookProfiles.front().BuiltIn);

    const std::vector<CookProfile> resolved = ResolveCookProfiles(loaded.CookProfiles);
    EXPECT_EQ(resolved.size(), BuiltInCookProfiles().size() + 1u);
    EXPECT_NE(FindProfile(resolved, "full"), nullptr);
    EXPECT_NE(FindProfile(resolved, "fast_geometry"), nullptr);

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(CookProfileTest, ProjectProfileCannotShadowBuiltInId)
{
    JsonValue profile(JsonValue::Object{
        { "id", JsonValue("full") },
        { "name", JsonValue("Replacement Full") },
        { "steps", JsonValue(JsonValue::Array{}) },
        { "outputs", JsonValue(JsonValue::Object{}) },
    });
    JsonValue profiles(JsonValue::Array{ std::move(profile) });

    std::vector<CookProfile> parsed;
    std::string error;
    EXPECT_FALSE(ReadCookProfiles(profiles, parsed, &error));
    EXPECT_NE(error.find("duplicate cook profile id"), std::string::npos);
}
