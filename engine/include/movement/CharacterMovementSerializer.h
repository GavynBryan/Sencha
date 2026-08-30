#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <memory>

//=============================================================================
// CharacterMovement scene serializer
//
// The component holds a registration-order locomotion mode id, which means
// nothing outside the process that produced it. Content states the mode the
// way content always does -- by the name it was registered under -- so a
// TypeSchema cannot describe this component and a hand-written serializer
// does.
//
// The wire form is untouched and stays the id: a snapshot travels between two
// processes the identity gate has already proved are the same build, which a
// file is not.
//
// Hand this to ComponentRegistrar::AddSerializer beside the component's Add.
//=============================================================================

[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeCharacterMovementSerializer();
