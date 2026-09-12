#include "infraforge/application/validation/ValidatorRegistry.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace infraforge::application::validation {

void ValidatorRegistry::registerValidator(std::unique_ptr<Validator> validator) {
    if (!validator) {
        throw std::invalid_argument("cannot register a null validator");
    }
    const auto name = validator->name();
    if (name.empty()) {
        throw std::invalid_argument("validator name must be non-empty");
    }
    for (const auto& existing : validators_) {
        if (existing->name() == name) {
            throw std::invalid_argument("duplicate validator name: " + std::string{name});
        }
    }
    validators_.push_back(std::move(validator));
}

const std::vector<std::unique_ptr<Validator>>& ValidatorRegistry::validators() const noexcept {
    return validators_;
}

} // namespace infraforge::application::validation
