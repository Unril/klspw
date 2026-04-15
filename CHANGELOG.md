# Changelog

## 0.1.2

- Fixed composite build support: projects using `includeBuild` with `dependencySubstitution` now correctly resolve cross-root module dependencies
- Fixed databinding support: generated binding classes (`data_binding_base_class_source_out`) are now included as source roots
- Init script: detect `ProjectComponentIdentifier` in resolved artifacts for composite build dependency and coordinate mapping
- Init script: use `BuildIdentifier.buildPath` instead of removed `getName()` for Gradle 9.x compatibility

## 0.1.1

Internal code quality improvements. No changes to CLI behavior or output.

- Automated Homebrew tap updates via `update-tap.yml` workflow
- Improved code structure with better use of C++23 ranges and monadic optionals
- Replaced empty-string/path sentinel values with `std::optional` throughout

## 0.1.0

Initial release.
