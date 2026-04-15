# Roadmap

The primary goal is to make klspw work reliably across all Gradle project
configurations that kotlin-lsp supports.

## Focus areas

- Handle edge cases in multi-root, composite build, KMP, and Android projects
- Resolve all source sets, classpaths, and compiler plugin arguments correctly
- Improve error messages when Gradle builds fail or produce unexpected output
- Expand integration test coverage across more real-world project layouts

## Non-goals

- Replacing kotlin-lsp's built-in Gradle import for projects where it works
- Becoming a general-purpose Gradle analysis tool
- Supporting non-Kotlin JVM languages

## Feedback

If you hit a project configuration that klspw handles incorrectly, open an issue
on [GitHub](https://github.com/Unril/klspw/issues) with the Gradle project
structure and the error or incorrect output.
