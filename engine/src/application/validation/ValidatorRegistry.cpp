#include "infraforge/application/validation/ValidatorRegistry.hpp"

#include <utility>

namespace infraforge::application::validation {

void ValidatorRegistry::registerValidator(std::unique_ptr<Validator> validator) {
    validators_.push_back(std::move(validator));
}

const std::vector<std::unique_ptr<Validator>>& ValidatorRegistry::validators() const noexcept {
    return validators_;
}

} // namespace infraforge::application::validation
