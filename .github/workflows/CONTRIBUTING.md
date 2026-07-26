# Contributing to PalServerLogger

Thank you for your interest in contributing to **PalServerLogger**! Whether it's reporting a bug, improving documentation, or submitting a feature request, your help is always appreciated.

## How Can I Contribute?

### 1. Reporting Bugs & Issues
If you encounter any crashes, errors, or unexpected behavior while running the logger or mod loader:
* Check the existing issues on the [GitHub Issues Page](https://github.com/GlitchApotamus/PalServerLogger/issues) to avoid duplicates.
* Create a new issue and include clear details:
  * Server Info (OS, OS version/build, game server version)
  * Mod version
  * Description of the problem.
  * Steps to reproduce.
  * Relevant log files or error codes (including Windows error codes or console output).

### 2. Suggesting Enhancements
Have an idea for a new feature or optimization? 
* Open an issue and tag it as an **Enhancement** or **Feature Request**.
* Clearly describe the proposed feature and why it would be beneficial for Palworld server administrators.

### 3. Pull Requests
Contributions to the codebase, translation dictionaries, and documentation are welcome:
1. **Fork** the repository.
2. Create a new branch for your feature or bugfix (`git checkout -b feature/your-feature-name`).
3. Commit your changes using clear and descriptive messages.
4. Push your branch to your fork (`git push origin feature/your-feature-name`).
5. Open a **Pull Request** against the `main` branch of the original repository.

## Development Setup

* **Build System:** CMake (utilizing Visual Studio / MSBuild toolchains on Windows).
* **Configuration:** Built-in localization and dependencies (like MinHook and JSON libraries) are managed through CMake.
* **Building Locally:** 
  ```cmd
  cmake -B build -S .
  cmake --build build --config Release
  ```