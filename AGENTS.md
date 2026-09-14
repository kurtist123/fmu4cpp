# Development Rules & Constraints

## Code Modification & Refactoring Integrity
- **Atomic File Rewrites for Structural Changes**: For structural refactoring (modifying class hierarchies, changing inheritance, rewriting class declarations, removing members/vectors), use atomic full-file writes (`write_to_file` with `Overwrite: true`) rather than fragmented search-and-replace patches. Reserve `replace_file_content` strictly for small, localized 1-5 line edits to prevent line splicing and duplicate code blocks.
- **Remove Obsolete Code Immediately**: When refactoring or replacing structures, methods, or storage layouts, delete obsolete code and unused helpers immediately. Never leave dead code, duplicate destructors, or obsolete storage members behind.
- **Mandatory Diff Review**: Always inspect `git diff` after making modifications to ensure no duplicate definitions, conflicting chunks, or unintended changes exist.

## Diff Hygiene & Formatting Integrity
- **Preserve Existing Formatting**: Do not reformat code that does not need to change. Maintain surrounding indentation, whitespace, and brace styling.
- **Preserve Declaration & Method Ordering**: Do not move methods, functions, or variable declarations around without a functional necessity. Keep original declaration and definition positions intact.
- **Minimal Diff Footprint**: Only touch code that strictly requires modification. Never make speculative or stylistic changes across unaffected code.

## Build & Test Verification
- **Clean-First Builds for Header Changes**: Always verify header modifications with `cmake --build build --clean-first` to ensure no syntax or declaration errors are masked by cached translation units.
- **Run Full Test Suite**: Always execute `ctest --test-dir build/export/tests --output-on-failure` and schema validation (`python3 scripts/validate_fmu.py build/models/fmi3/*.fmu build/models/fmi2/*.fmu --schema-dir schemas/`) after modifying code.

## Version Control & Commit Protocol
- **No Autonomous Commits**: Never run `git commit` without explicit user permission. Prepare changes and let the user review them first.
- **No Conventional Commits**: Do not use conventional commit prefixes (e.g., `feat:`, `fix:`) unless explicitly requested.

## Project Architectural Constraints
- **FMI 2 Backward Compatibility**: Changes to `fmu_base` and core variable classes must remain strictly backward compatible with FMI 2.
- **Immutable Files**: Never modify `export/include/fmu4cpp/logger.hpp`.
- **Documentation Location**: Project documentation must reside in `/root/dev/fmi3/docs/`, never in `fmu4cpp/docs/`.
