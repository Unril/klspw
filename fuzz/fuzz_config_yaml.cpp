// Fuzz target for klspw YAML config parsing.
// Exercises ConfigData::from_yaml() with arbitrary input to find crashes,
// assertion failures, or undefined behavior in the config parser.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "config.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  try {
    const std::string_view input(reinterpret_cast<const char*>(data), size);  // NOLINT
    auto cfg = klspw::ConfigData::from_yaml(input);
    (void)cfg;
  } catch (...) {  // NOLINT
                   // Expected: parse errors, validation failures. Not a bug.
  }
  return 0;
}
