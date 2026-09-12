#pragma once

#include <memory>

#include "infraforge/viewport/platform/NativeSurface.hpp"

namespace infraforge::viewport {

// Creates the platform surface implementation. On platforms without an
// implemented child-surface embedding this returns a stub whose create()
// fails explicitly with an actionable message; the process then reports
// renderer failure instead of pretending a surface exists.
[[nodiscard]] std::unique_ptr<NativeSurface> createPlatformSurface();

} // namespace infraforge::viewport
