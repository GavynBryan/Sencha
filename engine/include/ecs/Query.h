#pragma once

#include <ecs/ArchetypeSignature.h>
#include <ecs/Chunk.h>
#include <ecs/ComponentId.h>
#include <ecs/QueryAccessors.h>
#include <ecs/World.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

// ─── compile-time accessor index ─────────────────────────────────────────────

template <typename A, typename... Ts>
struct AccessorIndexOf;

template <typename A>
struct AccessorIndexOf<A> : std::integral_constant<size_t, 0> {};

template <typename A, typename Head, typename... Tail>
struct AccessorIndexOf<A, Head, Tail...>
    : std::integral_constant<size_t,
          std::is_same_v<A, Head>
              ? 0
              : 1 + AccessorIndexOf<A, Tail...>::value>
{};

// ─── ChunkView ───────────────────────────────────────────────────────────────
//
// Typed view over one chunk, yielded to system callbacks during ForEachChunk.
// ColIndices[i] is the column index in the chunk for accessor i.
// UINT32_MAX means "no column" (With, Without, Changed — no data column).

template <typename... Accessors>
struct ChunkView
{
    static constexpr size_t NAcc = sizeof...(Accessors);

    Chunk*                     RawChunk = nullptr;
    uint32_t                   Frame    = 0;
    std::array<uint32_t, NAcc> ColIndices{};

    const EntityIndex* Entities() const { return RawChunk->EntityIndices(); }
    uint32_t           Count()    const { return RawChunk->RowCount; }

    // Returns const span. Column version is NOT bumped (Read is non-mutating).
    template <typename T>
    std::span<const T> Read() const
    {
        constexpr size_t I = AccessorIndexOf<::Read<T>, Accessors...>::value;
        static_assert(I < NAcc, "Read<T> not in query accessor list");
        const uint32_t col = ColIndices[I];
        assert(col != UINT32_MAX && "Read<T>: column not found in chunk");
        return RawChunk->ColumnSpan<const T>(col);
    }

    // Returns mutable span.
    // Column version is bumped once per chunk by ForEachChunk after the callback
    // returns — conservative semantics: bump happens whether or not any row was
    // actually written. See docs/ecs/decisions.md D0.9.
    template <typename T>
    std::span<T> Write()
    {
        constexpr size_t I = AccessorIndexOf<::Write<T>, Accessors...>::value;
        static_assert(I < NAcc, "Write<T> not in query accessor list");
        const uint32_t col = ColIndices[I];
        assert(col != UINT32_MAX && "Write<T>: column not found in chunk");
        return RawChunk->ColumnSpan<T>(col);
    }
};

// ─── Query ───────────────────────────────────────────────────────────────────
//
// Durable, cached query parameterized by accessor types.
// Caches matching archetypes; rebuilds lazily when new archetypes are created.
//
// Usage:
//   Query<Read<Position>, Write<Velocity>, Without<Frozen>> q(world);
//   q.ForEachChunk([dt](auto& view) {
//       auto pos = view.template Read<Position>();
//       auto vel = view.template Write<Velocity>();
//       for (uint32_t i = 0; i < view.Count(); ++i)
//           vel[i].X += pos[i].X * dt;
//   });

template <typename... Accessors>
class Query
{
public:
    static constexpr size_t NAcc = sizeof...(Accessors);

    explicit Query(World& world) : W(&world)
    {
        BuildSignatures();
        RebuildMatchingArchetypes();
    }

    // Chunk-level iteration — primary entry point.
    // F: void(ChunkView<Accessors...>&)
    // referenceFrame: used by Changed<T> to determine "changed since when".
    //   Pass 0 to match all chunks with any write in their history.
    //   Pass world.CurrentFrame()-1 to match only chunks written last frame.
    template <typename F>
    void ForEachChunk(F&& fn, uint32_t referenceFrame = 0)
    {
        W->PushQueryScope();
        RebuildIfStale();

        const uint32_t frame = W->CurrentFrame();

        for (uint32_t archIdx : MatchingArchetypes)
        {
            Archetype& arch = *W->GetArchetypes()[archIdx];
            if (arch.Chunks.empty()) continue;

            // Column indices are identical for all chunks in the same archetype —
            // compute once per archetype, not per chunk. See decisions.md D0.6.
            ChunkView<Accessors...> view;
            view.Frame = frame;
            PopulateColIndices(view, *arch.Chunks[0], std::index_sequence_for<Accessors...>{});

            for (auto& chunkPtr : arch.Chunks)
            {
                Chunk& chunk = *chunkPtr;
                if (chunk.IsEmpty()) continue;
                if (!PassesChangedFilter(chunk, referenceFrame)) continue;

                view.RawChunk = &chunk;
                fn(view);

                BumpWriteVersions(chunk, view, frame, std::index_sequence_for<Accessors...>{});
            }
        }

        W->PopQueryScope();
    }

    void RebuildMatchingArchetypes()
    {
        MatchingArchetypes.clear();
        const auto& archetypes = W->GetArchetypes();
        for (uint32_t i = 0; i < static_cast<uint32_t>(archetypes.size()); ++i)
        {
            if (SignatureMatches(archetypes[i]->Signature, RequiredSig, ExcludedSig))
                MatchingArchetypes.push_back(i);
        }
        CachedArchetypeCount = static_cast<uint32_t>(archetypes.size());
    }

private:
    World* W;

    ArchetypeSignature RequiredSig;
    ArchetypeSignature ExcludedSig;
    ArchetypeSignature ChangedSig;

    // ComponentIds resolved once at construction — no hash-map lookup per chunk.
    // See decisions.md D0.5.
    std::array<ComponentId, NAcc> CachedIds{};

    std::vector<uint32_t> MatchingArchetypes;
    uint32_t              CachedArchetypeCount = 0;

    void BuildSignatures()
    {
        RequiredSig.reset();
        ExcludedSig.reset();
        ChangedSig.reset();
        (AddAccessorToSigs<Accessors>(), ...);
    }

    template <typename A>
    void AddAccessorToSigs()
    {
        if constexpr (IsRead<A>::value || IsWrite<A>::value || IsWith<A>::value)
        {
            const ComponentId id = W->template GetComponentId<typename A::Component>();
            RequiredSig.set(id);
            CachedIds[AccessorIndexOf<A, Accessors...>::value] = id;
        }
        else if constexpr (IsChanged<A>::value)
        {
            const ComponentId id = W->template GetComponentId<typename A::Component>();
            RequiredSig.set(id);
            ChangedSig.set(id);
            CachedIds[AccessorIndexOf<A, Accessors...>::value] = id;
        }
        else if constexpr (IsWithout<A>::value)
        {
            const ComponentId id = W->template GetComponentId<typename A::Component>();
            ExcludedSig.set(id);
            CachedIds[AccessorIndexOf<A, Accessors...>::value] = id;
        }
    }

    bool PassesChangedFilter(const Chunk& chunk, uint32_t referenceFrame) const
    {
        if (ChangedSig.none()) return true;
        for (uint32_t c = 0; c < chunk.ColumnCount; ++c)
        {
            if (!ChangedSig.test(chunk.Columns[c].Id)) continue;
            if (chunk.ColumnLastWrittenFrame(c) <= referenceFrame)
                return false;
        }
        return true;
    }

    template <size_t... Is>
    void PopulateColIndices(ChunkView<Accessors...>& view,
                            const Chunk& chunk,
                            std::index_sequence<Is...>)
    {
        (PopulateOne<Is, Accessors>(view, chunk), ...);
    }

    template <size_t I, typename A>
    void PopulateOne(ChunkView<Accessors...>& view, const Chunk& chunk)
    {
        if constexpr (IsRead<A>::value || IsWrite<A>::value)
            view.ColIndices[I] = chunk.FindColumn(CachedIds[I]);
        else
            view.ColIndices[I] = UINT32_MAX;
    }

    void RebuildIfStale()
    {
        if (W->GetArchetypes().size() != CachedArchetypeCount)
            RebuildMatchingArchetypes();
    }

    template <size_t... Is>
    void BumpWriteVersions(
        Chunk& chunk,
        const ChunkView<Accessors...>& view,
        uint32_t frame,
        std::index_sequence<Is...>)
    {
        (BumpOneWrite<Is, Accessors>(chunk, view, frame), ...);
    }

    template <size_t I, typename A>
    void BumpOneWrite(Chunk& chunk, const ChunkView<Accessors...>& view, uint32_t frame)
    {
        if constexpr (IsWrite<A>::value)
        {
            const uint32_t col = view.ColIndices[I];
            assert(col != UINT32_MAX);
            chunk.BumpColumnVersion(col, frame);
        }
    }
};
