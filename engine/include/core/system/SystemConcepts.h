#pragma once

//=============================================================================
// System capability concepts
//
// A system type opts into one or more dispatch lanes by implementing the
// corresponding method. No base class, no vtable.
//
//   HasUpdate  — frame lane. Update(float dt) called once per frame.
//   HasTick    — fixed lane. Tick(float fixedDt) called 0..N times per frame
//                at a constant step, driven by the physics accumulator.
//   HasRender  — render lane. Render(float alpha) called once per frame with
//                alpha = accumulator / fixedDt for interpolation.
//   HasInit    — optional Init() called by SystemHost::Init().
//   HasShutdown— optional Shutdown() called by SystemHost::Shutdown().
//
// IsSystem: satisfied if T participates in at least one lane.
//=============================================================================

template<typename T>
concept HasInit = requires(T& t) { t.Init(); };

template<typename T>
concept HasUpdate = requires(T& t, float dt) { t.Update(dt); };

template<typename T>
concept HasTick = requires(T& t, float dt) { t.Tick(dt); };

template<typename T>
concept HasRender = requires(T& t, float alpha) { t.Render(alpha); };

template<typename T>
concept HasShutdown = requires(T& t) { t.Shutdown(); };

template<typename T>
concept IsSystem = HasUpdate<T> || HasTick<T> || HasRender<T>;
