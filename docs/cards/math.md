# math

Snapshot `97ed48e4` (2026-10-30). Layer: foundation.

## 1. Frame phases

- None: a header-mostly library with no state, called from every phase.

## 2. Inputs

- Plain values only; response curves can be read from data by `ReadResponseCurve` (`engine/include/math/ResponseCurve.h:46`).
- Module dependencies: core only.

## 3. Outputs

- Vectors, matrices and quaternions, `Vec` (`engine/include/math/Vec.h:242`), `Mat` (`engine/include/math/Mat.h:26`), `Quat` (`engine/include/math/Quat.h:33`): used by 26 engine modules and 176 editor files.
- Geometry, `Aabb3d` (`engine/include/math/geometry/3d/Aabb3d.h:10`), `Frustum` (`engine/include/math/geometry/3d/Frustum.h:11`), `Transform3d` (`engine/include/math/geometry/3d/Transform3d.h:29`): consumed by render, navigation, zone, assets and the editor.

## 4. Central decisions

- `Frustum::IntersectsAabb` (`engine/include/math/geometry/3d/Frustum.h:25`): decides whether a box can be on screen, which is the render culling test.
- `ClipConvexPolygon` (`engine/src/math/geometry/3d/Aabb3d.cpp:104`): decides which part of a convex polygon lies inside a box.
- `EvaluateResponseCurve` (`engine/include/math/ResponseCurve.h:39`): maps an input in 0..1 through an authored curve shape.

## 5. Invariants

- Matrix multiplication composes in matrix order: `Quat.MultiplicationComposesInMatrixOrder` (`test/math_geometry/QuatTests.cpp`).
- Transforms compose right then left: `Transform2d.CompositionAppliesRightThenLeft` (`test/math_geometry/TransformTests.cpp`).
- Direction factories are right-handed: `Vec.DirectionConventionIsRightHanded` (`test/math_geometry/VecTests.cpp`).
- Common vector sizes have tight storage: `Vec.CommonVectorSizesHaveTightFieldStorage` (`test/math_geometry/VecTests.cpp`).
- An empty box is invalid until something is added: `Aabb.EmptyFactoryCreatesInvalidAccumulationBox` (`test/math_geometry/AabbTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 3,454 lines in 37 files.
- CCN 16 `ReadResponseCurve` (`engine/src/math/ResponseCurve.cpp:78`); CCN 12 `ClipConvexPolygon` (`engine/src/math/geometry/3d/Aabb3d.cpp:104`); CCN 7 `TransformAabb` (`engine/src/math/geometry/3d/AabbTransform.cpp:3`).

## 7. Docs that contradict the code

- None found.
