#pragma once

/// Human-readable description context for model types.
///
/// DescribeContext accumulates output lines, controls verbosity, and manages
/// path shortening for cache jar display. Model types call describe(ctx) to
/// append their representation. The caller retrieves lines via lines().

#include <spdlog/spdlog.h>

#include "common.hpp"
#include "strings.hpp"

namespace klspw {

class DescribeContext;

/// A type that can describe itself into a DescribeContext.
template <typename T>
concept Describable = requires(const T& t, DescribeContext& ctx) { t.describe(ctx); };

class DescribeContext {
  public:
    explicit DescribeContext(bool verbose = true, string_views path_markers = {})
        : verbose_{verbose},
          path_markers_{path_markers} {}

    /// Append a formatted line.
    void add(string line) { lines_.push_back(std::move(line)); }

    /// Whether verbose detail should be included.
    bool verbose() const { return verbose_; }

    /// Format a path for display, shortening cache paths using configured markers.
    /// Records stripped prefixes for later summary via flush_stripped_prefixes().
    string format_path(string_view path) {
        auto [display, stripped] = strip_prefixes(path, path_markers_);
        if (!stripped.empty()) {
            stripped_prefixes_.emplace(stripped);
            return format(".../{}", display);
        }
        return string{display};
    }

    /// Add a section header and describe each element in the range.
    template <r::input_range R>
        requires Describable<r::range_value_t<R>>
    void describe_section(string header, const R& range) {
        add(std::move(header));
        describe(range);
    }

    /// Describe each element in a range of Describable elements.
    template <r::input_range R>
        requires Describable<r::range_value_t<R>>
    void describe(const R& range) {
        for (const auto& elem : range) {
            elem.describe(*this);
        }
    }

    /// Describe a single Describable element.
    template <Describable T> void describe(const T& item) { item.describe(*this); }

    /// Describe an optional element. No-op if empty.
    template <Describable T> void describe(const optional<T>& item) {
        if (item) {
            item->describe(*this);
        }
    }

    /// Flush accumulated cache prefix summary lines.
    void flush_stripped_prefixes() {
        for (const auto& prefix : stripped_prefixes_) {
            add(format("  (cache: {})", prefix));
        }
    }

    /// Log all accumulated lines at the given level.
    void log(spdlog::level::level_enum level) const {
        for (const auto& line : lines_) {
            spdlog::log(level, "{}", line);
        }
    }

    /// Read accumulated lines.
    const strings& lines() const { return lines_; }

    /// Discard all accumulated lines and stripped prefixes.
    void clear() {
        lines_.clear();
        stripped_prefixes_.clear();
    }

  private:
    strings lines_;
    bool verbose_;
    string_views path_markers_;
    set<string> stripped_prefixes_;
};

} // namespace klspw
