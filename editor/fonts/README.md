# Editor fonts

Bundled fonts for the Sencha level-editor UI. All are permissively licensed and
redistributable; the license texts live alongside the files.

| File | Role | Family | License | Source |
|------|------|--------|---------|--------|
| `Inter-Regular.ttf`, `Inter-Medium.ttf` | UI body / labels | Inter 4.1 | SIL OFL 1.1 (`LICENSE-Inter.txt`) | rsms/inter release `extras/ttf` (static, hinted) |
| `JetBrainsMono-Regular.ttf` | Console / monospace | JetBrains Mono | SIL OFL 1.1 (`LICENSE-JetBrainsMono.txt`) | JetBrains/JetBrainsMono `fonts/ttf` |
| `fa-solid-900.ttf` | Toolbar / status icons | Font Awesome 6 Free Solid | Fonts: SIL OFL 1.1; icon designs: CC-BY 4.0 (`LICENSE-FontAwesome.txt`) | FortAwesome/Font-Awesome `webfonts` |
| `IconsFontAwesome6.h` | `ICON_FA_*` codepoint macros for the atlas merge | — | Zlib/MIT (juliettef/IconFontCppHeaders) | header only, no font data |

All four TTFs were verified to carry a `glyf` table (static TrueType outlines) so
ImGui's bundled `stb_truetype` rasterizes them directly — no variable-font default
instance or CFF/OpenType outline issues.

The SIL OFL requires the license/copyright to ship with the font files (done here)
and forbids selling the fonts on their own. Font Awesome's free icon *designs* are
CC-BY 4.0: attribution is satisfied by keeping `LICENSE-FontAwesome.txt`.
