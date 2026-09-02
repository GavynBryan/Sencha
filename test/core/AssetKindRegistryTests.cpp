// What the front door will accept as an asset kind. Every driver downstream --
// the scanner, the preloader, the hot reloader, the lease API -- reads these
// records and trusts their shape, so a half-wired kind has to be refused here
// rather than found by a null dereference three layers later.

#include <gtest/gtest.h>

#include <core/assets/AssetKindRegistry.h>
#include <core/assets/AssetLease.h>
#include <core/assets/AssetStore.h>
#include <core/assets/AssetStager.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace
{
    class NullStager final : public IAssetStager
    {
    public:
        AssetStaging LoadStaged(const AssetRecord&, IAssetSource&) override { return {}; }
    };

    class NullStore : public IAssetListStore
    {
    public:
        explicit NullStore(AssetType type)
            : Kind(type)
        {
        }

        AssetType Type() const override { return Kind; }
        bool IsResident(std::string_view) const override { return false; }
        AssetLease TryAcquireLease(std::string_view) override { return {}; }
        std::string_view GetPath(std::uint64_t) const override { return {}; }
        void RetainToken(std::uint64_t) override {}
        void ReleaseToken(std::uint64_t) override {}
        AssetLease InternList(std::span<const std::uint64_t>) override { return {}; }
        std::vector<std::uint64_t> ListMembers(std::uint64_t) const override { return {}; }

    private:
        AssetType Kind;
    };

    // A kind wired the way a host with a cache for it wires one.
    AssetKindRegistration LoadableKind(AssetType type, IAssetStager& stager, IAssetStore& store)
    {
        AssetKindRegistration kind = MakeBuiltinAssetKind(type);
        kind.Stager = &stager;
        kind.Store = &store;
        kind.Commit = [](AssetStaging&&) { return AssetLease{}; };
        return kind;
    }
}

TEST(AssetKindRegistry, AKindIsFoundByTypeAndByExtension)
{
    NullStager stager;
    NullStore materials(AssetType::Material);

    AssetKindRegistry kinds;
    ASSERT_TRUE(kinds.Register(LoadableKind(AssetType::Material, stager, materials)));

    const AssetKindRegistration* kind = kinds.Find(AssetType::Material);
    ASSERT_NE(kind, nullptr);
    EXPECT_TRUE(kind->IsLoadable());
    ASSERT_FALSE(kind->RuntimeExtensions.empty());
    EXPECT_EQ(kinds.FindTypeByExtension(kind->RuntimeExtensions.front()), AssetType::Material);
    EXPECT_EQ(kinds.FindTypeByExtension(".nothing"), AssetType::Unknown);
}

TEST(AssetKindRegistry, OneTypeAndOneExtensionBelongToOneKind)
{
    NullStager stager;
    NullStore materials(AssetType::Material);
    NullStore audio(AssetType::Audio);

    AssetKindRegistry kinds;
    ASSERT_TRUE(kinds.Register(LoadableKind(AssetType::Material, stager, materials)));
    EXPECT_FALSE(kinds.Register(LoadableKind(AssetType::Material, stager, materials)));

    // A second kind claiming the first one's extension would make a scan's
    // classification depend on registration order.
    AssetKindRegistration stealsExtension = LoadableKind(AssetType::Audio, stager, audio);
    stealsExtension.RuntimeExtensions = kinds.Find(AssetType::Material)->RuntimeExtensions;
    EXPECT_FALSE(kinds.Register(std::move(stealsExtension)));
}

// Staging touches no cache, so a host that cannot hold a kind still registers
// the stage half; the commit half is what tracks whether a cache exists.
TEST(AssetKindRegistry, StagingWithoutACacheIsRegisterableButNotLoadable)
{
    NullStager stager;

    AssetKindRegistration kind = MakeBuiltinAssetKind(AssetType::StaticMesh);
    kind.Stager = &stager;
    AssetKindRegistry kinds;
    ASSERT_TRUE(kinds.Register(std::move(kind)));
    EXPECT_FALSE(kinds.Find(AssetType::StaticMesh)->IsLoadable());
}

TEST(AssetKindRegistry, AStoreAndItsCommitTravelTogether)
{
    NullStager stager;
    NullStore materials(AssetType::Material);

    AssetKindRegistration storeOnly = MakeBuiltinAssetKind(AssetType::Material);
    storeOnly.Stager = &stager;
    storeOnly.Store = &materials;

    AssetKindRegistry kinds;
    EXPECT_FALSE(kinds.Register(std::move(storeOnly)));

    AssetKindRegistration commitOnly = MakeBuiltinAssetKind(AssetType::Material);
    commitOnly.Stager = &stager;
    commitOnly.Commit = [](AssetStaging&&) { return AssetLease{}; };
    EXPECT_FALSE(kinds.Register(std::move(commitOnly)));
}

TEST(AssetKindRegistry, AStoreHoldsItsOwnKind)
{
    NullStager stager;
    NullStore audio(AssetType::Audio);

    AssetKindRegistry kinds;
    EXPECT_FALSE(kinds.Register(LoadableKind(AssetType::Material, stager, audio)));
}

// A list is made of the single store's tokens, so it cannot be registered
// without one, and it names the same kind.
TEST(AssetKindRegistry, AListStoreNeedsTheSingleStoreOfItsOwnKind)
{
    NullStager stager;
    NullStore materials(AssetType::Material);
    NullStore audio(AssetType::Audio);

    AssetKindRegistration listOnly = MakeBuiltinAssetKind(AssetType::Material);
    listOnly.Stager = &stager;
    listOnly.ListStore = &materials;

    AssetKindRegistry kinds;
    EXPECT_FALSE(kinds.Register(std::move(listOnly)));

    AssetKindRegistration wrongKind = LoadableKind(AssetType::Material, stager, materials);
    wrongKind.ListStore = &audio;
    EXPECT_FALSE(kinds.Register(std::move(wrongKind)));

    AssetKindRegistration wired = LoadableKind(AssetType::Material, stager, materials);
    wired.ListStore = &materials;
    ASSERT_TRUE(kinds.Register(std::move(wired)));
    EXPECT_EQ(kinds.Find(AssetType::Material)->ListStore, &materials);
}

TEST(AssetKindRegistry, ReloadRequiresTheLoadHalfItSwapsIntoPlace)
{
    NullStager stager;

    AssetKindRegistration kind = MakeBuiltinAssetKind(AssetType::Material);
    kind.Stager = &stager;
    kind.Reload = [](AssetStaging&&) { return true; };

    AssetKindRegistry kinds;
    EXPECT_FALSE(kinds.Register(std::move(kind)));
}

TEST(AssetKindRegistry, AnUnknownTypeHasNoIdentityToRegister)
{
    AssetKindRegistry kinds;
    EXPECT_FALSE(kinds.Register(MakeBuiltinAssetKind(AssetType::Unknown)));
}

// Iteration order is registration order, which is what keeps a content scan's
// classification and its diagnostics stable between runs.
TEST(AssetKindRegistry, EntriesComeBackInRegistrationOrder)
{
    NullStager stager;
    NullStore materials(AssetType::Material);
    NullStore audio(AssetType::Audio);

    AssetKindRegistry kinds;
    ASSERT_TRUE(kinds.Register(LoadableKind(AssetType::Audio, stager, audio)));
    ASSERT_TRUE(kinds.Register(LoadableKind(AssetType::Material, stager, materials)));

    const std::span<const AssetKindRegistration> entries = kinds.Entries();
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].Type, AssetType::Audio);
    EXPECT_EQ(entries[1].Type, AssetType::Material);
}
