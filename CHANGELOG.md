# Changelog

All notable changes to PalServerLogger are documented in this file. This changelog is based on the project’s Git history and release tags.

## Unreleased

### Changed
- Removed the legacy `UsePalDefender` config toggle.
- `load_dlls` is now the sole source of truth for injected DLLs.
- The loader now normalizes the config file on every startup:
  - unknown keys are removed
  - missing supported keys are restored
  - user-defined `load_dlls` entries are preserved instead of being replaced with defaults

---

## 1.0.7

### Changed
- Refactored Nexus upload workflow with file ID validation and streamlined upload handling.
- Improved Nexus mod verification with clearer game-context and error messaging.
- Added explicit Nexus verification steps and better version handling.

## 1.0.6

### Added
- Improved log configuration features for filename timestamp formatting and invalid-character sanitization.

### Changed
- Updated release workflow handling and packaging flow for compatible assets.

## 1.0.5-build12

### Added
- Added filename timestamp format support for log files.
- Sanitized invalid Windows filename characters automatically.

## 1.0.5

### Changed
- Upgraded checkout actions and improved workflow behavior.
- Updated changelog generation format and general release automation.
- Adjusted asset handling in the GitHub Actions release flow.

## 1.0.4

### Added
- Added support for `logger_config.json` with customizable log settings.
- Added asynchronous logging support.
- Added automatic defaults for missing config fields without overwriting existing user values.

## 1.0.3

### Added
- Added dynamic translation support for the loader prefix and other user-facing strings.

### Changed
- Updated version metadata and release packaging.

## 1.0.2

### Added
- Added GitHub Actions workflow for automated releases.
- Added release asset handling and publishing automation.

## 1.0.1

### Changed
- Updated the internal version number to 0.1 and finalized the initial package metadata alignment.

## 1.0

### Changed
- Bumped the project version to 1.0.
- Updated Nexus upload configuration and API handling for mod publishing.

## 0.9

### Changed
- Updated the Nexus API key flow and release version metadata.
- Improved release workflow compatibility for uploaded files.

## 0.8

### Changed
- Bumped the release version to 0.8.
- Adjusted release packaging and workflow metadata.

## 0.7

### Changed
- Updated release workflow and version metadata to 0.7.
- Improved overall release automation.

## 0.6

### Changed
- Updated release process and version metadata to 0.6.
- Improved automation and workflow clarity.

## 0.5

### Added
- Added configurable filename timestamp format support for logs.
- Improved release workflow automation and GitHub changelog generation.

### Changed
- Updated the CMake minimum required version to 3.20.

## 0.4

### Added
- Added mod configuration support for runtime logging customization.
- Added support for this config to be created automatically and merged safely.

## 0.3

### Added
- Added translation support for the loader prefix and message strings.

### Changed
- Updated version metadata to 0.3.

## 0.2

### Added
- Added localization and translation support to the mod loader.
- Added runtime text customization through generated config values.

### Changed
- Updated the README and loader to explain the localization features and configuration flow.
- Bumped version to 0.2.

## 0.1

### Added
- Initial project release.
- Added core loader functionality and initial automated release pipeline.
- Added project metadata and initial repository setup.

---

This changelog is intended to be GitHub-friendly and can be referenced directly in releases and pull requests.
