#pragma once

#include "infraforge/application/validation/Validator.hpp"

#include <memory>
#include <string>
#include <vector>

namespace infraforge::application::validation {

// Ordered registry of validators. Validators execute in registration order,
// producing a deterministic combined diagnostic stream. The registry owns
// its validators and is not domain-specific: it has no knowledge of roads,
// terrain, or any particular validation rule.
//
// Registration happens once at engine startup; the registry is not modified
// at runtime. This keeps validation runs deterministic and reproducible.
class ValidatorRegistry {
public:
    ValidatorRegistry() = default;

    ValidatorRegistry(const ValidatorRegistry&) = delete;
    ValidatorRegistry& operator=(const ValidatorRegistry&) = delete;

    // Registers a validator. Validators are executed in registration order.
    // The registry takes ownership.
    void registerValidator(std::unique_ptr<Validator> validator);

    // Returns the registered validators in execution order.
    [[nodiscard]] const std::vector<std::unique_ptr<Validator>>& validators() const noexcept;

private:
    std::vector<std::unique_ptr<Validator>> validators_;
};

} // namespace infraforge::application::validation
