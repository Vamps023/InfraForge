#pragma once

#include <string>

namespace infraforge::runtime {

// Current UTC time as "YYYY-MM-DDTHH:MM:SSZ" (ISO 8601). Used for project
// timestamps persisted in project.json and project_state.
[[nodiscard]] std::string utcTimestampNow();

} // namespace infraforge::runtime
