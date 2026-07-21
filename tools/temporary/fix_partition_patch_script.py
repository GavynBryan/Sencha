from pathlib import Path

path = Path("tools/temporary/apply_partition_world_patch.py")
text = path.read_text()

needle = '''def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"expected one match, found {count}: {old[:100]!r}")
    text = text.replace(old, new, 1)
'''
replacement = needle + '''\n\ndef replace_first(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count < 1:
        raise RuntimeError(f"expected at least one match, found {count}: {old[:100]!r}")
    text = text.replace(old, new, 1)
'''
if text.count(needle) != 1:
    raise RuntimeError("replace_once helper changed unexpectedly")
text = text.replace(needle, replacement, 1)

# The first member of each repeated pair is intentionally changed to
# replace_first. The second call remains replace_once and verifies that exactly
# one duplicate remains after the first replacement.
markers = [
    "# Preserve partition on all type-erased structural paths.",
    "replace_once(\n'''        assert(Entities.IsAlive(entity));",
    'replace_once(\n    "        auto [dci, dri] = dst->AddRow(entity.Index);\\n",',
    'replace_once(\n    "        Entities.SetLocation(entity, EntityLocation{ dst->Id, dci, dri });\\n",',
    "replace_once(\n'''        ++StructuralCounter;\n\n        struct Move",
    "replace_once(\n'''            EntityLocation loc = Entities.GetLocation(entity);",
    'replace_once(\n    "            auto [dci, dri] = dst->AddRow(entity.Index);\\n",',
    'replace_once(\n    "                EntityLocation{ dst->Id, dci, dri }\\n",',
]

start = text.index(markers[0])
for marker in markers[1:]:
    index = text.index(marker, start)
    text = text[:index] + text[index:].replace("replace_once(", "replace_first(", 1)
    start = index + len("replace_first(")

path.write_text(text)
