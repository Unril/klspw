// SPDX-FileCopyrightText: 2026 Nikolai Fedorov
//
// SPDX-License-Identifier: MIT

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
    return r::to<std::vector>();
  } else {
    return r::to<std::vector<T>>();
  }
}

template <typename T = void>
constexpr auto to_set() {
  if constexpr (std::is_void_v<T>) {
    return r::to<std::unordered_set>();
  } else {
    return r::to<std::unordered_set<T>>();
  }
}

/// Concatenate multiple ranges into a single vector.
/// Element type is deduced from the first range.
/// Usage: concat_to_vector(range1 | v::transform(f), range2 | v::filter(g))
template <r::input_range First, r::input_range... Rest>
auto concat_to_vector(First&& first, Rest&&... rest) {
  auto result = std::forward<First>(first) | to_vector();
  (result.append_range(std::forward<Rest>(rest)), ...);
  return result;
}

/// A callable that maps a value to an optional result.
template <typename Fn, typename Elem>
concept OptionalMapper = requires(Fn fn, Elem elem) {
  { fn(elem) } -> std::same_as<optional<typename std::invoke_result_t<Fn, Elem>::value_type>>;
};

/// find_map(range, fn) -- apply fn to each element, return the first non-empty optional.
/// Equivalent to Rust's Iterator::find_map. fn must return optional<T>.
template <r::input_range R, OptionalMapper<r::range_value_t<R>> Fn>
auto find_map(R&& range, Fn fn) {
  using Result = std::invoke_result_t<Fn, r::range_value_t<R>>;
  for (auto&& elem : std::forward<R>(range)) {
    if (auto result = fn(elem)) {
      return result;
    }
  }
  return Result{};
}

/// find_opt(range, pred) -- like r::find_if but returns optional<value> instead of iterator.
template <r::input_range R, typename Pred>
  requires std::predicate<Pred, const r::range_value_t<R>&>
auto find_opt(R&& range, Pred pred) -> optional<r::range_value_t<R>> {
  const auto it = r::find_if(std::forward<R>(range), pred);
  if (it != r::end(range)) {
    return *it;
  }
  return nullopt;
}

}  // namespace klspw
