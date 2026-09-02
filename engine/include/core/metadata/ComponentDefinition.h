#pragma once

#include <cstdint>
#include <string_view>

// The generated descriptor a component's declarative facts live in, written by
// sencha-component-codegen from the annotations on the component itself.
//
// TypeSchema, ComponentTypeKey, ComponentRemovable and ComponentEditorVisual
// project from this, so consumers never name it. Behavior stays handwritten:
// ComponentTraits, ComponentStorageTraits and SceneFieldCodec are neither
// emitted nor read here.
template <typename T>
struct ComponentDefinition;

template <typename T>
concept HasComponentDefinition = requires { ComponentDefinition<T>::Identity; };

// A component may be authored without being persisted, so each fact is asked
// for separately rather than assumed to travel with the others.
template <typename T>
concept DefinitionHasSchema = HasComponentDefinition<T>
    && requires { ComponentDefinition<T>::SchemaName; ComponentDefinition<T>::Fields(); };

template <typename T>
concept DefinitionHasSceneChunk = HasComponentDefinition<T>
    && requires { ComponentDefinition<T>::SceneChunk; };

template <typename T>
concept DefinitionIsReplicated = HasComponentDefinition<T>
    && requires { ComponentDefinition<T>::Replicated; };

template <typename T>
concept DefinitionIsPredicted = HasComponentDefinition<T>
    && requires { ComponentDefinition<T>::Predicted; };

// The asset path rather than a built EditorVisual, so this header stays clear
// of EditorVisual.h -- which includes this one to do the projecting.
template <typename T>
concept DefinitionHasVisualMesh = HasComponentDefinition<T>
    && requires { ComponentDefinition<T>::VisualMeshAsset; };

// Stamped into every companion and checked against it, so a generator older
// than the headers reading its output fails at compile rather than silently.
inline constexpr std::uint32_t kComponentCodegenFormatVersion = 1;
