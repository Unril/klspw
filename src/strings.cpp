#include "strings.hpp"

namespace klspw {

opt_string extract_between(string_view input, Delimiters delimiters) {
  delimiters.validate();
  const auto begin_pos = input.find(delimiters.open);
  if (begin_pos == string_view::npos) {
    return nullopt;
  }
  const auto content_start = begin_pos + delimiters.open.size();
  const auto end_pos = input.find(delimiters.close, content_start);
  if (end_pos == string_view::npos) {
    return nullopt;
  }
  return string{trim(input.substr(content_start, end_pos - content_start))};
}

}  // namespace klspw
