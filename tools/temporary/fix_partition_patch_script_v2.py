from pathlib import Path

path = Path("tools/temporary/apply_partition_world_patch.py")
text = path.read_text()

start_marker = "# Preserve partition on all type-erased structural paths."
end_marker = "replace_once(\n'''    uint64_t StructuralVersion() const { return StructuralCounter; }"
start = text.index(start_marker)
end = text.index(end_marker, start)
section = text[start:end]

remaining = section.count("replace_once(")
if remaining == 0:
    raise RuntimeError("type-erased patch section already has no asserted replacements")

# The section is deliberately ordered to mirror AddComponentRaw,
# RemoveComponentRaw, AddComponentsRawBatch, and RemoveComponentsRawBatch.
# Each replacement still requires at least one source match through
# replace_first; ordering supplies the disambiguation that raw text cannot.
section = section.replace("replace_once(", "replace_first(")
text = text[:start] + section + text[end:]
path.write_text(text)
