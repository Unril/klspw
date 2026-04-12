// Fuzz target for klspw Gradle output parsing.
// Exercises GradleBuildOutput::from_raw_output() with arbitrary input to find
// crashes or undefined behavior in the delimiter extraction and JSON parser.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "gradle.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    try {
        const std::string_view input(reinterpret_cast<const char*>(data), size); // NOLINT
        auto out = klspw::GradleBuildOutput::from_raw_output(input);
        (void)out;
    } catch (...) { // NOLINT
        // Expected: missing delimiters, JSON parse errors. Not a bug.
    }
    return 0;
}
