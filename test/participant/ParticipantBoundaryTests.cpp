#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    std::string Read(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        std::ostringstream contents;
        contents << input.rdbuf();
        return contents.str();
    }

    std::vector<std::filesystem::path> SourcesUnder(
        const std::filesystem::path& root)
    {
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        {
            if (entry.is_regular_file()
                && (entry.path().extension() == ".h"
                    || entry.path().extension() == ".cpp"))
            {
                files.push_back(entry.path());
            }
        }
        return files;
    }

    void ExpectAbsent(const std::filesystem::path& root,
                      std::initializer_list<std::string_view> forbidden)
    {
        for (const std::filesystem::path& path : SourcesUnder(root))
        {
            const std::string source = Read(path);
            for (const std::string_view token : forbidden)
            {
                EXPECT_EQ(source.find(token), std::string::npos)
                    << path << " crosses the participant/network boundary with "
                    << token;
            }
        }
    }
}

TEST(ParticipantBoundary, ParticipantMechanismHasNoNetworkDependency)
{
    const std::filesystem::path root = SENCHA_REPO_ROOT;
    ExpectAbsent(root / "engine/include/participant",
                 { "#include <net/", "PeerId", "NetOwner", "NetDrivenBy",
                   "NetReplicated" });
    ExpectAbsent(root / "engine/src/participant",
                 { "#include <net/", "PeerId", "NetOwner", "NetDrivenBy",
                   "NetReplicated" });
}

TEST(ParticipantBoundary, NetworkMechanismDoesNotReachIntoParticipantState)
{
    const std::filesystem::path root = SENCHA_REPO_ROOT;
    ExpectAbsent(root / "engine/include/net", { "#include <participant/" });
    ExpectAbsent(root / "engine/src/net", { "#include <participant/" });
}
