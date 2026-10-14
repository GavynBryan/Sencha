#pragma once

#include <core/metadata/RuntimeSchema.h>

#include <optional>
#include <string>
#include <string_view>

//=============================================================================
// A schema leaf as presented text, and back.
//
// An authored surface presents a component field as the string somebody reads
// and types, because presentation is text: a number being edited is whatever
// has been typed so far, and "1.2.3" is a thing to refuse on the way back in
// rather than something a document should have an opinion about.
//
// Kept apart from the surface that uses it so the rounding, the degree
// conversion and the refusals are testable with no document, window or device
// in the way -- these are the parts that are actually easy to get wrong.
//=============================================================================

// A schema name as a row label: the last dotted segment, underscores as spaces,
// each word capitalized and run-together words apart ("local.position" ->
// "Position", "play_on_active" -> "Play On Active", "PointLight" -> "Point
// Light"). Display only -- the widget id, the serialization and the
// binding all keep the raw name.
[[nodiscard]] std::string HumanizeSchemaName(std::string_view dotted);

// What a surface labels this field: what the field declares, or its humanized
// name when it declares nothing.
[[nodiscard]] std::string RuntimeFieldLabel(const RuntimeField& field);

// Whether an authoring surface can offer an editor for this leaf at all. False
// for an identity the schema marks read-only, for a leaf the descriptor cannot
// express, and for an asset handle -- which is refcounted and session-local, so
// it travels through its own command rather than through bytes.
[[nodiscard]] bool IsRuntimeFieldEditable(const RuntimeField& field);

// What this field currently holds, formatted for display. A multi-scalar leaf
// (a position, a rotation) comes back comma-separated, in the order the schema
// lays the scalars out; an inline-text leaf comes back as its characters, which
// is why the comma rule applies to numbers and not to text. Empty for a leaf
// with nothing presentable.
//
// `componentBytes` is the start of the component; the field's own offset is
// applied here.
[[nodiscard]] std::string FormatRuntimeField(const RuntimeField& field,
                                             const void* componentBytes);

// Writes `text` into the field, or refuses it.
//
// All-or-nothing: the scalars are parsed into a local buffer first, so a
// three-component vector whose third number is nonsense leaves all three bytes
// as they were rather than half-applying the edit.
[[nodiscard]] bool ParseRuntimeField(const RuntimeField& field,
                                     std::string_view text,
                                     void* componentBytes);

// One number of a numeric leaf -- a position's Y, or a plain number as index
// 0 -- read as text and written alone, the others keeping what they hold.
// Empty, or refused, for an index the leaf does not have or a leaf that is not
// numbers.
[[nodiscard]] std::string FormatRuntimeFieldElement(const RuntimeField& field,
                                                    const void* componentBytes,
                                                    std::size_t index);
[[nodiscard]] bool ParseRuntimeFieldElement(const RuntimeField& field,
                                            std::size_t index,
                                            std::string_view text,
                                            void* componentBytes);
// A one-number leaf's value in the units it is shown in (degrees for an angle
// held as radians); empty for text, a choice, a switch or several numbers.
[[nodiscard]] std::optional<double> RuntimeFieldNumber(const RuntimeField& field, const void* componentBytes);
// The number at `index` moved by `delta`, in the units it is shown in.
[[nodiscard]] bool OffsetRuntimeFieldElement(const RuntimeField& field,
                                             std::size_t index,
                                             double delta,
                                             void* componentBytes);
