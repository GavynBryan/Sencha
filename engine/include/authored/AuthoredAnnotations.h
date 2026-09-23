#pragma once

#include <ecs/ComponentAnnotations.h>

// Annotations that expose C++ declarations to authored content. Read only by
// sencha-component-codegen; empty in a normal compile. Usage and rules:
// docs/gameplay/authored-api.md.

// On a method.
#define SENCHA_VERB(identity)        SENCHA_ANNOTATE("sencha.verb=" identity)
#define SENCHA_QUERY(identity)       SENCHA_ANNOTATE("sencha.query=" identity)
#define SENCHA_DESCRIPTION(text)     SENCHA_ANNOTATE("sencha.description=" text)
#define SENCHA_CATEGORY(text)        SENCHA_ANNOTATE("sencha.category=" text)

// On a parameter.
#define SENCHA_ARG(key)              SENCHA_ANNOTATE("sencha.arg=" key)
#define SENCHA_RANGE(min, max)       SENCHA_ANNOTATE("sencha.range=" #min "," #max)
#define SENCHA_TARGET_1(key)         SENCHA_ANNOTATE("sencha.target=" key)
#define SENCHA_TARGET_2(key, type)                                             \
    SENCHA_ANNOTATE("sencha.target=" key)                                      \
    SENCHA_ANNOTATE("sencha.target_component=" #type)
#define SENCHA_TARGET_PICK(_1, _2, chosen, ...) chosen
// SENCHA_TARGET(key) or SENCHA_TARGET(key, Component).
#define SENCHA_TARGET(...)                                                     \
    SENCHA_TARGET_PICK(__VA_ARGS__, SENCHA_TARGET_2, SENCHA_TARGET_1, unused)(__VA_ARGS__)

// On a struct. SENCHA_QUERY also applies to component members.
#define SENCHA_EVENT(identity)       SENCHA_ANNOTATE("sencha.event=" identity)
#define SENCHA_EVENT_SOURCE(type)    SENCHA_ANNOTATE("sencha.event_source=" #type)
