#pragma once

/// Range adaptors: to_vector, unique_by, not_in.

#include "common.hpp"

namespace klspw {

/// Dedup elements by key, keeping first occurrence. Pipe adaptor: range | unique_by(proj)
namespace detail {

/// A type T is hashable if std::hash<T>{}(val) produces a std::size_t.
template <typename T>
concept Hashable = requires(T val) {
  { std::hash<T>{}(val) } -> std::convertible_to<std::size_t>;
};

template <typename Proj>
struct unique_by_adaptor : r::range_adaptor_closure<unique_by_adaptor<Proj>> {
  Proj proj;

  template <r::input_range R>
    requires Hashable<std::remove_cvref_t<std::invoke_result_t<Proj, const r::range_value_t<R>&>>>
  auto operator()(R&& range) const {
    using Val = r::range_value_t<R>;
    using Key = std::remove_cvref_t<std::invoke_result_t<Proj, const Val&>>;
    std::unordered_set<Key> seen;
    vector<Val> result;
    for (auto&& elem : std::forward<R>(range)) {
      if (seen.insert(std::invoke(proj, elem)).second) {
        result.push_back(std::forward<decltype(elem)>(elem));
      }
    }
    return result;
  }
};

}  // namespace detail

/// range | unique_by(&Type::field) -- dedup keeping first occurrence per key.
template <typename Proj = std::identity>
  requires(!r::input_range<Proj>)
auto unique_by(Proj proj = {}) {
  return detail::unique_by_adaptor<Proj>{.proj = std::move(proj)};
}

/// Returns a filter that excludes elements present in the given set.
/// Usage: source_roots | not_in(resources_roots) | ...
/// The returned view captures excluded by reference -- caller must ensure the set outlives the
/// view. Safe when used in a single expression that materializes with to_vector().
[[nodiscard]] inline auto not_in(const string_set& excluded) {
  return v::filter([&excluded](const auto& val) { return !excluded.contains(val); });
}

/// Pipe adaptor: range | to_vector() materializes into a vector.
template <typename T = void>
constexpr auto to_vector() {
  if constexpr (std::is_void_v<T>) {
    return r::to<vector>();
  } else {
    return r::to<vector<T>>();
  }
}

}  // namespace klspw
