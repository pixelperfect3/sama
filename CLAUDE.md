# Sama Engine — Claude Code Instructions

## Git Workflow

- **Always push after committing.** Run `git push` immediately after every `git commit`.
- Commit messages: `type(scope): short description` (e.g., `feat(scene): add TransformSystem`)
- Small, focused commits — one logical change per commit.

## Code Style

- C++20, Allman braces, 4-space indent, 100 char line limit
- Run `clang-format -i` on every C++ file after writing or editing
- `UpperCamelCase` classes, `camelCase` functions/variables, `ALL_CAPS` constants, `trailing_` for private members
- Namespaces: `engine::subsystem` (e.g., `engine::scene`, `engine::rendering`)

## Testing

- Write tests alongside implementation, not after
- Use Catch2 v3 (`catch2/catch_test_macros.hpp`)
- Screenshot tests: run with `--update-goldens` to regenerate reference images
- Run relevant tests before committing: `build/engine_tests "[tag]"`

## Build

- CMake build directory: `build/`
- Build a target: `cmake --build build --target <name> -j$(sysctl -n hw.ncpu)`
- Key targets: `engine_tests`, `engine_screenshot_tests`, `helmet_demo`, `hierarchy_demo`

## Documentation

- Update `docs/NOTES.md` when making architectural decisions — include reasoning and tradeoffs, not just summaries
- Architecture docs go in `docs/` (e.g., `SCENE_GRAPH_ARCHITECTURE.md`)
- App-specific READMEs go in the app directory (e.g., `apps/hierarchy_demo/README.md`)
