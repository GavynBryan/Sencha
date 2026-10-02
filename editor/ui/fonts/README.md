# Editor fonts

Bundled fonts for Kyusu's authored documents, which declare them with
`@font-face` and get them as cooked `.sfont` assets. All are permissively
licensed and redistributable; the license texts live alongside the files.

| File | Role | Family | License | Source |
|------|------|--------|---------|--------|
| `JetBrainsMono-Regular.ttf` | numbers, paths and chords | JetBrains Mono | SIL OFL 1.1 (`LICENSE-JetBrainsMono.txt`) | JetBrains/JetBrainsMono `fonts/ttf` |
| `ChakraPetch-SemiBold.ttf` | titles, labels and readouts | Chakra Petch | SIL OFL 1.1 (`LICENSE-ChakraPetch.txt`) | google/fonts `ofl/chakrapetch` |
| `Inter-Regular.ttf`, `Inter-Medium.ttf` | body text | Inter 4.1 | SIL OFL 1.1 (`LICENSE-Inter.txt`) | rsms/inter release `extras/ttf` (static, hinted) |
| `fa-solid-900.ttf` | the rail's glyphs (`.glyph`, by codepoint) | Font Awesome 6 Free Solid | Fonts: SIL OFL 1.1; icon designs: CC-BY 4.0 (`LICENSE-FontAwesome.txt`) | FortAwesome/Font-Awesome `webfonts` |

All five TTFs carry a `glyf` table (static TrueType outlines), so FreeType
rasterizes them without a variable-font default instance or CFF outlines.

The SIL OFL requires the license/copyright to ship with the font files (done here)
and forbids selling the fonts on their own. Font Awesome's free icon *designs* are
CC-BY 4.0: attribution is satisfied by keeping `LICENSE-FontAwesome.txt`.
