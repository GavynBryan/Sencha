#pragma once

// Declarative facts about a component, read by sencha-component-codegen, which
// emits the ComponentDefinition the engine projects from.
//
// Only the generator defines SENCHA_CODEGEN, so in every real build these
// expand to nothing and leave layout, size and ABI untouched -- which matters,
// because a component's layout is module-facing contract.
//
// One annotation per fact: there is no grammar to parse, and a mistyped one is
// a compiler error at the declaration rather than something the generator has
// to detect and explain.
//
//   struct SENCHA_COMPONENT("sencha.physics.character_controller")
//          SENCHA_SCHEMA("CharacterController")
//          SENCHA_SCENE_CHUNK("CHCT")
//   CharacterController
//   {
//       SENCHA_FIELD("radius") float Radius = 0.3f;
//   };

#if defined(SENCHA_CODEGEN)
#  define SENCHA_ANNOTATE(text) __attribute__((annotate(text)))
#else
#  define SENCHA_ANNOTATE(text)
#endif

// The identity whose hash names the component in cooked content and in the
// replication handshake. Frozen once content exists.
#define SENCHA_COMPONENT(identity)   SENCHA_ANNOTATE("sencha.identity=" identity)

// What its schema is called: the scene key, and what reflection calls it.
#define SENCHA_SCHEMA(name)          SENCHA_ANNOTATE("sencha.schema=" name)

#define SENCHA_SCENE_CHUNK(fourcc)   SENCHA_ANNOTATE("sencha.scene_chunk=" fourcc)
#define SENCHA_REPLICATED            SENCHA_ANNOTATE("sencha.replicated")
#define SENCHA_PREDICTED             SENCHA_ANNOTATE("sencha.predicted")
#define SENCHA_NON_REMOVABLE         SENCHA_ANNOTATE("sencha.non_removable")
#define SENCHA_VISUAL_MESH(asset)    SENCHA_ANNOTATE("sencha.visual_mesh=" asset)

// Opt-in: an untagged member is not part of the schema, so adding an ordinary
// member never silently changes the scene format or the replication layout.
#define SENCHA_FIELD(name)           SENCHA_ANNOTATE("sencha.field=" name)

#define SENCHA_OWNER_ONLY            SENCHA_ANNOTATE("sencha.owner_only")
#define SENCHA_OWNER_LOCAL           SENCHA_ANNOTATE("sencha.owner_local")
#define SENCHA_LOCAL_ONLY            SENCHA_ANNOTATE("sencha.local_only")
#define SENCHA_COLOR                 SENCHA_ANNOTATE("sencha.color")
#define SENCHA_DEGREES               SENCHA_ANNOTATE("sencha.degrees")
#define SENCHA_OPTIONAL              SENCHA_ANNOTATE("sencha.optional")
#define SENCHA_ASSET(kind)           SENCHA_ANNOTATE("sencha.asset=" kind)
#define SENCHA_ASSET_LIST(kind)      SENCHA_ANNOTATE("sencha.asset_list=" kind)
#define SENCHA_DATA_ASSET(subtype)   SENCHA_ANNOTATE("sencha.data_asset=" subtype)
#define SENCHA_LABEL(text)           SENCHA_ANNOTATE("sencha.label=" text)
#define SENCHA_TOOLTIP(text)         SENCHA_ANNOTATE("sencha.tooltip=" text)

#define SENCHA_QUANTIZE(min, max, bits) \
    SENCHA_ANNOTATE("sencha.quantize=" #min "," #max "," #bits)
