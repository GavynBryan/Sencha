#pragma once

#include <ecs/ComponentAnnotations.h>

// Annotations that expose C++ declarations to authored content. Read only by
// sencha-component-codegen; empty in a normal compile. Usage and rules:
// docs/gameplay/authored-api.md.

#define SENCHA_ANNOTATION_PICK(_1, _2, chosen, ...) chosen

// On a method.
#define SENCHA_VERB(identity)        SENCHA_ANNOTATE("sencha.verb=" identity)
#define SENCHA_QUERY(identity)       SENCHA_ANNOTATE("sencha.query=" identity)
#define SENCHA_LEASE(identity)       SENCHA_ANNOTATE("sencha.lease=" identity)
#define SENCHA_LEASE_RELEASE(identity) SENCHA_ANNOTATE("sencha.lease_release=" identity)
#define SENCHA_LEASE_HOLDS(identity) SENCHA_ANNOTATE("sencha.lease_holds=" identity)
#define SENCHA_DESCRIPTION(text)     SENCHA_ANNOTATE("sencha.description=" text)
#define SENCHA_CATEGORY(text)        SENCHA_ANNOTATE("sencha.category=" text)
#define SENCHA_COSMETIC              SENCHA_ANNOTATE("sencha.cosmetic")
// An AuthoredPrediction enumerator: SENCHA_PREDICTION(Simulation).
#define SENCHA_PREDICTION(prediction) SENCHA_ANNOTATE("sencha.prediction=" #prediction)
#define SENCHA_COMPLETES             SENCHA_ANNOTATE("sencha.completes")
#define SENCHA_RESULT_1(key)         SENCHA_ANNOTATE("sencha.result=" key)
#define SENCHA_RESULT_2(key, type)                                             \
    SENCHA_ANNOTATE("sencha.result=" key)                                      \
    SENCHA_ANNOTATE("sencha.result_component=" #type)
// SENCHA_RESULT(key) or SENCHA_RESULT(key, Component), on a verb returning VerbResult.
#define SENCHA_RESULT(...)                                                     \
    SENCHA_ANNOTATION_PICK(__VA_ARGS__, SENCHA_RESULT_2, SENCHA_RESULT_1, unused)(__VA_ARGS__)

// On a parameter. SENCHA_LABEL, SENCHA_DESCRIPTION, SENCHA_ASSET and
// SENCHA_DATA_ASSET apply here too.
#define SENCHA_ARG(key)              SENCHA_ANNOTATE("sencha.arg=" key)
#define SENCHA_RANGE(min, max)       SENCHA_ANNOTATE("sencha.range=" #min "," #max)
#define SENCHA_MIN(value)            SENCHA_ANNOTATE("sencha.min=" #value)
#define SENCHA_MAX(value)            SENCHA_ANNOTATE("sencha.max=" #value)
#define SENCHA_UNITS(text)           SENCHA_ANNOTATE("sencha.units=" text)
// The editor widget a parameter's row uses: SENCHA_WIDGET("direction").
#define SENCHA_WIDGET(name)          SENCHA_ANNOTATE("sencha.widget=" name)
#define SENCHA_TARGET_1(key)         SENCHA_ANNOTATE("sencha.target=" key)
#define SENCHA_TARGET_2(key, type)                                             \
    SENCHA_ANNOTATE("sencha.target=" key)                                      \
    SENCHA_ANNOTATE("sencha.target_component=" #type)
// SENCHA_TARGET(key) or SENCHA_TARGET(key, Component).
#define SENCHA_TARGET(...)                                                     \
    SENCHA_ANNOTATION_PICK(__VA_ARGS__, SENCHA_TARGET_2, SENCHA_TARGET_1, unused)(__VA_ARGS__)

// On a struct. SENCHA_QUERY also applies to component members.
#define SENCHA_EVENT(identity)       SENCHA_ANNOTATE("sencha.event=" identity)
#define SENCHA_EVENT_SOURCE(type)    SENCHA_ANNOTATE("sencha.event_source=" #type)
// A result of SENCHA_FIELD members, answered whole.
#define SENCHA_RECORD                SENCHA_ANNOTATE("sencha.record")
