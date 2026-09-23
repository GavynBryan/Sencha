#pragma once

#include <ecs/ComponentAnnotations.h>

//=============================================================================
// Authored API annotations
//
// What exposes a C++ declaration to authored content. Like the component
// annotations, these expand to nothing in a normal compile and only say
// something to sencha-component-codegen, which reads them and writes the
// adapters into the header's companion. They describe a contract; they never
// register, bind or run anything. Declaring the vocabulary and binding an
// implementation stay explicit calls a person can find.
//
// Identity is always written out. The generator infers only what the compiler
// already knows -- parameter and return types, constness, default arguments --
// and never derives a persisted name from a C++ one, because C++ names are
// refactorable and content contracts are not.
//
//   class DoorSystem
//   {
//   public:
//       SENCHA_VERB("door.set_locked") SENCHA_LABEL("Set locked")
//       VerbAdmission SetLocked(SENCHA_TARGET("door", Door) EntityId door,
//                               SENCHA_ARG("locked") bool locked);
//   };
//
// On a method:
//   SENCHA_VERB(identity)     an operation content may invoke; returns
//                             VerbAdmission, may take a leading
//                             `const VerbInvocation&` for its provenance
//   SENCHA_QUERY(identity)    a const question content may ask
//   SENCHA_LABEL, SENCHA_DESCRIPTION, SENCHA_CATEGORY   presentation
//
// On a parameter (every one but the leading invocation needs one of the first
// two):
//   SENCHA_ARG(key)           an argument, by its persisted key
//   SENCHA_TARGET(key[, C])   an entity argument; with C, one the author is
//                             expected to pick from entities carrying component
//                             C. Authoring metadata: the implementation still
//                             checks what it was handed.
//   SENCHA_RANGE(min, max)    the numeric range the argument accepts
//   SENCHA_LABEL(text)        presentation
//
// On a component member:
//   SENCHA_QUERY(name)        readable by authored content as
//                             "<component identity>.<name>". Independent of
//                             SENCHA_FIELD: a member can be queryable without
//                             being serialized, and serialized without being
//                             queryable.
//
// On a struct:
//   SENCHA_EVENT(identity)    a typed event gameplay code publishes; its
//                             SENCHA_FIELD members are the payload
//   SENCHA_EVENT_SOURCE(C)    the component a source entity is expected to
//                             carry. Authoring metadata, like a target's.
//=============================================================================

#define SENCHA_VERB(identity)        SENCHA_ANNOTATE("sencha.verb=" identity)
#define SENCHA_QUERY(identity)       SENCHA_ANNOTATE("sencha.query=" identity)
#define SENCHA_EVENT(identity)       SENCHA_ANNOTATE("sencha.event=" identity)
#define SENCHA_EVENT_SOURCE(type)    SENCHA_ANNOTATE("sencha.event_source=" #type)
#define SENCHA_DESCRIPTION(text)     SENCHA_ANNOTATE("sencha.description=" text)
#define SENCHA_CATEGORY(text)        SENCHA_ANNOTATE("sencha.category=" text)
#define SENCHA_ARG(key)              SENCHA_ANNOTATE("sencha.arg=" key)
#define SENCHA_RANGE(min, max)       SENCHA_ANNOTATE("sencha.range=" #min "," #max)

#define SENCHA_TARGET_1(key)         SENCHA_ANNOTATE("sencha.target=" key)
#define SENCHA_TARGET_2(key, type)                                             \
    SENCHA_ANNOTATE("sencha.target=" key)                                      \
    SENCHA_ANNOTATE("sencha.target_component=" #type)
#define SENCHA_TARGET_PICK(_1, _2, chosen, ...) chosen
#define SENCHA_TARGET(...)                                                     \
    SENCHA_TARGET_PICK(__VA_ARGS__, SENCHA_TARGET_2, SENCHA_TARGET_1, unused)(__VA_ARGS__)
