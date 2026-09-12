#pragma once

#include <string>

namespace infraforge::domain {

// Field-scoped validation failure shared by domain validation functions.
struct ValidationError {
    std::string field;
    std::string message;
};

} // namespace infraforge::domain
