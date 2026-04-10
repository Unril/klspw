#pragma once

/// Validation context for collecting errors across a config hierarchy.
///
/// ValidateContext accumulates error messages instead of throwing on the first one.
/// Types call validate(ctx) to check their fields and delegate to children.
/// schema_only controls whether semantic checks run.

#include "common.hpp"
#include "strings.hpp"

namespace klspw {

class ValidateContext;

/// A type that can validate itself into a ValidateContext.
template <typename T>
concept Validatable = requires(const T& t, ValidateContext& ctx) { t.validate(ctx); };

class ValidateContext {
  public:
    explicit ValidateContext(bool schema_only = false) : schema_only_{schema_only} {}

    /// Record an error if condition is false.
    void check(bool condition, string_view message) {
        if (!condition) {
            errors_.emplace_back(message);
        }
    }

    /// Whether to skip semantic checks (only validate schema/required fields).
    bool schema_only() const { return schema_only_; }

    /// Validate each element in a range of Validatable elements.
    template <r::input_range R>
        requires Validatable<r::range_value_t<R>>
    void validate(const R& range) {
        for (const auto& elem : range) {
            elem.validate(*this);
        }
    }

    /// Validate a single Validatable element.
    template <Validatable T> void validate(const T& item) { item.validate(*this); }

    /// Validate an optional element. No-op if empty.
    template <Validatable T> void validate(const optional<T>& item) {
        if (item) {
            item->validate(*this);
        }
    }

    bool has_errors() const { return !errors_.empty(); }

    const strings& errors() const { return errors_; }

    /// Throw runtime_error with all collected errors if any exist.
    void throw_if_errors() const {
        if (errors_.empty()) {
            return;
        }
        throw runtime_error(join(errors_, "; "));
    }

  private:
    strings errors_;
    bool schema_only_;
};

} // namespace klspw
