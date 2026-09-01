#pragma once

//=============================================================================
// Component annotations
//
// Declarative facts about a component, stated on the component's own
// declaration and read by sencha-component-codegen, which emits the
// ComponentDefinition the rest of the engine projects from.
//
// They compile to nothing. Only the generator defines SENCHA_CODEGEN, so in
// every real build -- GCC, Clang, any warning level -- these expand to empty and
// leave the type's layout, size and ABI untouched. That is load-bearing: a
// component's memory layout is module-facing contract.
//
// One annotation per fact, on purpose. Each carries a single string with no
// grammar to parse, and a mistyped annotation is a compiler error at the
// declaration rather than something the generator has to detect and explain.
//
// These describe; they never implement. Lifecycle (ComponentTraits), structural
// registration (ComponentStorageTraits) and field persistence (SceneFieldCodec)
// stay handwritten beside the component, and the generator neither emits nor
// reads them.
//
//   struct SENCHA_COMPONENT("sencha.physics.character_controller")
//          SENCHA_SCHEMA("CharacterController")
//          SENCHA_SCENE_CHUNK("CHCT")
//   CharacterController
//   {
//       SENCHA_FIELD("radius") float Radius = 0.3f;
//   };
//=============================================================================

#if defined(SENCHA_CODEGEN)
#  define SENCHA_ANNOTATE(text) __attribute__((annotate(text)))
#else
#  define SENCHA_ANNOTATE(text)
#endif

// ─── On the component ────────────────────────────────────────────────────────

// The component's stable identity: the string whose hash names it in cooked
// content and in the replication handshake. Frozen once content exists.
#define SENCHA_COMPONENT(identity)   SENCHA_ANNOTATE("sencha.identity=" identity)

// The name its schema is known by -- the scene key, and what the editor and
// runtime reflection call it. Separate from the identity because the two
// genuinely differ for most components, and separate from the scene chunk
// because a component may be authored without being persisted.
#define SENCHA_SCHEMA(name)          SENCHA_ANNOTATE("sencha.schema=" name)

// The component persists into scenes, under this four-character chunk id.
#define SENCHA_SCENE_CHUNK(fourcc)   SENCHA_ANNOTATE("sencha.scene_chunk=" fourcc)

// The component's values travel from an authority to its peers.
#define SENCHA_REPLICATED            SENCHA_ANNOTATE("sencha.replicated")

// The owning peer resumes simulating from the authority's value. Implies
// replicated; the registrar rejects the pair if it is not.
#define SENCHA_PREDICTED             SENCHA_ANNOTATE("sencha.predicted")

// A tool may not remove this component from an entity.
#define SENCHA_NON_REMOVABLE         SENCHA_ANNOTATE("sencha.non_removable")

// The editor draws an entity carrying this component as the given mesh.
#define SENCHA_VISUAL_MESH(asset)    SENCHA_ANNOTATE("sencha.visual_mesh=" asset)

// ─── On a member ─────────────────────────────────────────────────────────────

// The member is authored and serialized under this name. Opt-in: an untagged
// member is not part of the schema, so adding an ordinary member never silently
// changes the scene format or the replication layout.
#define SENCHA_FIELD(name)           SENCHA_ANNOTATE("sencha.field=" name)

// Sent only to the peer that owns the entity.
#define SENCHA_OWNER_ONLY            SENCHA_ANNOTATE("sencha.owner_only")

// Sent to everyone except the owning peer, which holds a fresher answer.
#define SENCHA_OWNER_LOCAL           SENCHA_ANNOTATE("sencha.owner_local")

// Never leaves the machine that computed it.
#define SENCHA_LOCAL_ONLY            SENCHA_ANNOTATE("sencha.local_only")

// An RGB colour, so an inspector shows a swatch rather than three drag fields.
#define SENCHA_COLOR                 SENCHA_ANNOTATE("sencha.color")

// Radians stored, degrees authored.
#define SENCHA_DEGREES               SENCHA_ANNOTATE("sencha.degrees")

// The member may be absent from content; its member initializer stands.
#define SENCHA_OPTIONAL              SENCHA_ANNOTATE("sencha.optional")

// An owning reference to a resident asset of the given kind.
#define SENCHA_ASSET(kind)           SENCHA_ANNOTATE("sencha.asset=" kind)
#define SENCHA_ASSET_LIST(kind)      SENCHA_ANNOTATE("sencha.asset_list=" kind)
#define SENCHA_DATA_ASSET(subtype)   SENCHA_ANNOTATE("sencha.data_asset=" subtype)

// The range the member occupies and the precision worth sending.
#define SENCHA_QUANTIZE(min, max, bits) \
    SENCHA_ANNOTATE("sencha.quantize=" #min "," #max "," #bits)

// Display metadata for an inspector row.
#define SENCHA_LABEL(text)           SENCHA_ANNOTATE("sencha.label=" text)
#define SENCHA_TOOLTIP(text)         SENCHA_ANNOTATE("sencha.tooltip=" text)
