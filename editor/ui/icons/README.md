# Editor icons

One SVG per `IconId` (`editor/common/src/icons/IconId.h`), named by the id in
kebab case (`grid-frame.svg` for `IconId::GridFrame`); `IconName` and
`IconByName` map between the two. Each file is cooked into a texture with the
editor UI, and `editor/ui/icons.rcss` gives it the class `icon-<name>` that
documents show it by. A new icon is a file, a line in `icons.rcss`, an id and
its name; `IconNames.EveryIconIsFoundByItsNameAndHasItsFileAndClass` checks the
three agree.

Convention: `viewBox="0 0 24 24"`; shapes in white (`#ffffff`), which the
document tints by theme role with `image-color`; strokes and fills only, no
text or gradients. They are hand-drawn editor data, not registry assets.
