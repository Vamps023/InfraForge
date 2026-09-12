#pragma once

#include <memory>

#include "infraforge/viewport/platform/NativeSurface.hpp"

namespace infraforge::viewport {

// Creates the platform surface implementation. On platforms without an
// implemented child-surface embedding this returns a stub whose create()
// fails explicitly with an actionable message; the process then reports
// renderer failure instead of pretending a surface exists.
[[nodiscard]] std::unique_ptr<NativeSurface> createPlatformSurface();

// Opts the process into the platform's best DPI-awareness mode (per-monitor
// v2 on Windows) and must run before any window is created, so the child
// surface tracks the host monitor's scale instead of being DPI-virtualized.
// Returns whether the process is DPI-aware afterwards; platforms without a
// process awareness concept return true.
[[nodiscard]] bool enablePlatformDpiAwareness();

} // namespace infraforge::viewport
