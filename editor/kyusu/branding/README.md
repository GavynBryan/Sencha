# Kyusu branding

The product's own marks. Not theme art and not icons: a theme never replaces
these, and they carry no `IconId`.

| File | Used for |
| --- | --- |
| `kyusu-logo.svg` | the product mark; the authored rail does not show it yet |
| `kyusu-icon.png` | the window icon, set once at startup |
| `kyusu.ico` | the Windows executable icon, compiled in as a resource |

`kyusu-logo.svg` is a flat white silhouette, like the files in `editor/ui/icons`,
so a document can tint it with a theme role (`.theme-art`). Keep it white and
keep it to paths: gradients and color in the file would be thrown away.

`kyusu-icon.png` and `kyusu.ico` are raster, because that is what the window
manager and Explorer want. They are rendered artwork rather than a silhouette,
so they are not tinted by anything.

All three are hand-replaceable: drop in a new file of the same name. The source
artwork lives outside the repository.
