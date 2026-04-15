// SPDX-FileCopyrightText: 2026 Nikolai Fedorov
//
// SPDX-License-Identifier: MIT

#pragma once

/// Structured logging for model types.
///
/// Logging levels:
///   info  -- what's going on: summaries, counts, progress
///   debug -- scalar fields and small collections
///   trace -- all collections (classpaths, source roots, etc.)
///
/// Model types implement void describe() const.

#include <spdlog/spdlog.h>

#include "common.hpp"

namespace klspw {

/// A type that can describe itself via logging.
template <typename T>
concept Describable = requires(const T& t) { t.describe(); };

/// Log at info level with lazy arg evaluation.
template <typename... Args>
void d_info(spdlog::format_string_t<detail::eval_t<Args>...> fmt, Args&&... args) {
  if (spdlog::should_log(spdlog::level::info)) {
    spdlog::info(fmt, detail::eval(std::forward<Args>(args))...);
  }
}

/// Log at debug level with lazy arg evaluation.
template <typename... Args>
void d_debug(spdlog::format_string_t<detail::eval_t<Args>...> fmt, Args&&... args) {
  if (spdlog::should_log(spdlog::level::debug)) {
    spdlog::debug(fmt, detail::eval(std::forward<Args>(args))...);
  }
}

/// Log at trace level with lazy arg evaluation.
template <typename... Args>
void d_trace(spdlog::format_string_t<detail::eval_t<Args>...> fmt, Args&&... args) {
  if (spdlog::should_log(spdlog::level::trace)) {
    spdlog::trace(fmt, detail::eval(std::forward<Args>(args))...);
  }
}

/// Log at warn level with lazy arg evaluation.
template <typename... Args>
void d_warn(spdlog::format_string_t<detail::eval_t<Args>...> fmt, Args&&... args) {
  if (spdlog::should_log(spdlog::level::warn)) {
    spdlog::warn(fmt, detail::eval(std::forward<Args>(args))...);
  }
}

/// Log at error level with lazy arg evaluation.
template <typename... Args>
void d_error(spdlog::format_string_t<detail::eval_t<Args>...> fmt, Args&&... args) {
  if (spdlog::should_log(spdlog::level::err)) {
    spdlog::error(fmt, detail::eval(std::forward<Args>(args))...);
  }
}

/// Log at critical level with lazy arg evaluation.
template <typename... Args>
void d_critical(spdlog::format_string_t<detail::eval_t<Args>...> fmt, Args&&... args) {
  if (spdlog::should_log(spdlog::level::critical)) {
    spdlog::critical(fmt, detail::eval(std::forward<Args>(args))...);
  }
}

/// Describe a single element.
template <Describable T>
void d_describe(const T& item) {
  item.describe();
}

/// Describe an optional element. No-op if empty.
template <typename T>
void d_describe(const optional<T>& item) {
  if (item) {
    d_describe(*item);
  }
}

/// Describe each element in a range of Describable elements.
template <r::input_range R>
  requires Describable<r::range_value_t<R>>
void d_describe(const R& range) {
  for (const auto& elem : range) {
    d_describe(elem);
  }
}

}  // namespace klspw
