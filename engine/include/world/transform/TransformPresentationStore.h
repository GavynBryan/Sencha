#pragma once

#include <math/geometry/3d/Transform3d.h>
#include <world/entity/EntityId.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformStore.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

template <typename TTransform>
struct TransformPresentationComponent
{
    TTransform PreviousLocal = TTransform::Identity();
    TTransform CurrentLocal = TTransform::Identity();
    TTransform RenderLocal = TTransform::Identity();
    TTransform RenderWorld = TTransform::Identity();
};

template <typename T>
Quat<T> SlerpShortest(const Quat<T>& a, const Quat<T>& b, T t)
{
    Quat<T> end = b;
    T dot = a.Dot(end);
    if (dot < T{0})
    {
        end = -end;
        dot = -dot;
    }

    if (dot > T{0.9995})
    {
        return Quat<T>{
            a.X + t * (end.X - a.X),
            a.Y + t * (end.Y - a.Y),
            a.Z + t * (end.Z - a.Z),
            a.W + t * (end.W - a.W),
        }.Normalized();
    }

    dot = std::clamp(dot, T{-1}, T{1});
    const T theta0 = std::acos(dot);
    const T theta = theta0 * t;
    const T sinTheta = std::sin(theta);
    const T sinTheta0 = std::sin(theta0);
    const T s0 = std::cos(theta) - dot * sinTheta / sinTheta0;
    const T s1 = sinTheta / sinTheta0;
    return Quat<T>{
        s0 * a.X + s1 * end.X,
        s0 * a.Y + s1 * end.Y,
        s0 * a.Z + s1 * end.Z,
        s0 * a.W + s1 * end.W,
    };
}

template <typename T>
Transform3d<T> InterpolateLocalTransform(const Transform3d<T>& previous,
                                         const Transform3d<T>& current,
                                         T alpha)
{
    return Transform3d<T>{
        Vec<3, T>::Lerp(previous.Position, current.Position, alpha),
        SlerpShortest(previous.Rotation, current.Rotation, alpha),
        Vec<3, T>::Lerp(previous.Scale, current.Scale, alpha),
    };
}

//=============================================================================
// TransformPresentationStore<TTransform>
//
// Render-facing transform snapshots for interpolation. Authoritative local/world
// transforms remain in TransformStore; this cache mirrors local simulation
// snapshots, interpolates locals by alpha, then propagates render-world
// transforms through the hierarchy. Reset() collapses previous/current/render to
// the authoritative transform to avoid smearing across discontinuities.
//=============================================================================
template <typename TTransform>
class TransformPresentationStore
{
public:
    void Reset(const TransformStore<TTransform>& authoritative,
               const TransformHierarchyService& hierarchy,
               TransformPropagationOrderService& order)
    {
        RebuildFromAuthoritative(authoritative);
        const auto authItems = authoritative.GetItems();
        for (std::size_t i = 0; i < Components.size() && i < authItems.size(); ++i)
        {
            Components[i].PreviousLocal = authItems[i].Local;
            Components[i].CurrentLocal = authItems[i].Local;
            Components[i].RenderLocal = authItems[i].Local;
        }

        PropagateRenderWorld(authoritative, hierarchy, order);
    }

    void CapturePreviousFromAuthoritative(const TransformStore<TTransform>& authoritative)
    {
        RebuildFromAuthoritative(authoritative);
        const auto authItems = authoritative.GetItems();
        for (std::size_t i = 0; i < Components.size() && i < authItems.size(); ++i)
        {
            Components[i].PreviousLocal = authItems[i].Local;
        }
    }

    void BeginSimulationTick(const TransformStore<TTransform>& authoritative)
    {
        CapturePreviousFromAuthoritative(authoritative);
    }

    void CaptureCurrentFromAuthoritative(const TransformStore<TTransform>& authoritative)
    {
        RebuildFromAuthoritative(authoritative);
        const auto authItems = authoritative.GetItems();
        for (std::size_t i = 0; i < Components.size() && i < authItems.size(); ++i)
        {
            Components[i].CurrentLocal = authItems[i].Local;
        }
    }

    void EndSimulationTick(const TransformStore<TTransform>& authoritative)
    {
        CaptureCurrentFromAuthoritative(authoritative);
    }

    void BuildRenderSnapshot(const TransformStore<TTransform>& authoritative,
                             const TransformHierarchyService& hierarchy,
                             TransformPropagationOrderService& order,
                             float alpha)
    {
        InterpolateAndPropagate(authoritative, hierarchy, order, alpha);
    }

    void InterpolateAndPropagate(const TransformStore<TTransform>& authoritative,
                                 const TransformHierarchyService& hierarchy,
                                 TransformPropagationOrderService& order,
                                 float alpha)
    {
        RebuildFromAuthoritative(authoritative);
        alpha = std::clamp(alpha, 0.0f, 1.0f);
        for (auto& component : Components)
        {
            component.RenderLocal = InterpolateLocalTransform(
                component.PreviousLocal,
                component.CurrentLocal,
                alpha);
        }

        PropagateRenderWorld(authoritative, hierarchy, order);
    }

    [[nodiscard]] const TTransform* TryGetWorld(EntityId entity) const
    {
        if (!entity.IsValid())
            return nullptr;

        for (std::size_t i = 0; i < Owners.size(); ++i)
        {
            if (Owners[i] == entity.Index)
                return &Components[i].RenderWorld;
        }
        return nullptr;
    }

    [[nodiscard]] std::span<const TransformPresentationComponent<TTransform>> GetItems() const
    {
        return std::span<const TransformPresentationComponent<TTransform>>(
            Components.data(), Components.size());
    }

private:
    void RebuildFromAuthoritative(const TransformStore<TTransform>& authoritative)
    {
        const auto& owners = authoritative.GetOwners();
        if (owners == Owners && Components.size() == authoritative.Count())
            return;

        Owners = owners;
        Components.resize(authoritative.Count());
        const auto authItems = authoritative.GetItems();
        for (std::size_t i = 0; i < Components.size() && i < authItems.size(); ++i)
        {
            Components[i].PreviousLocal = authItems[i].Local;
            Components[i].CurrentLocal = authItems[i].Local;
            Components[i].RenderLocal = authItems[i].Local;
            Components[i].RenderWorld = authItems[i].World;
        }
    }

    void PropagateRenderWorld(const TransformStore<TTransform>& authoritative,
                              const TransformHierarchyService& hierarchy,
                              TransformPropagationOrderService& order)
    {
        order.MaybeRebuild(hierarchy, authoritative);
        for (const auto& entry : order.GetOrder())
        {
            auto& component = Components[entry.TransformIndex];
            if (entry.ParentTransformIndex == TransformPropagationOrderService::NullIndex)
            {
                component.RenderWorld = component.RenderLocal;
            }
            else
            {
                component.RenderWorld =
                    Components[entry.ParentTransformIndex].RenderWorld * component.RenderLocal;
            }
        }
    }

    std::vector<Id> Owners;
    std::vector<TransformPresentationComponent<TTransform>> Components;
};
