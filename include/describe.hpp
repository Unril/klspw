#pragma once

/// Human-readable description context for model types.
///
/// DescribeContext accumulates output lines, controls verbosity, and manages
/// path shortening for cache jar display. Model types call describe(ctx) to
/// append their representation. The caller retrieves lines via take_lines().

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

    /// Shorten a path using configured markers. Records the stripped prefix.
    /// Returns the display string (shortened if a marker matched, original otherwise).
    string shorten_path(string_view path) {
        auto [display, stripped] = strip_prefixes(path, path_markers_);
        if (!stripped.empty()) {
            stripped_prefixes_.emplace(stripped);
            return format(".../{}", display);
        }
        return string{display};
    }

    /// Describe each element in a range of Describable elements.
    template <r::input_range R>
        requires Describable<r::range_value_t<R>>
    void describe_each(const R& range) {
        for (const auto& elem : range) {
            elem.describe(*this);
        }
    }

    /// Flush accumulated cache prefix summary lines.
    void flush_stripped_prefixes() {
        for (const auto& prefix : stripped_prefixes_) {
            add(format("  (cache: {})", prefix));
        }
    }

    /// Move accumulated lines out. Context is empty after this call.
    strings take_lines() { return std::move(lines_); }

  private:
    strings lines_;
    bool verbose_;
    string_views path_markers_;
    set<string> stripped_prefixes_;
};

} // namespace klspw
