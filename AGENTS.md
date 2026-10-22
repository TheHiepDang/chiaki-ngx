# Project conventions

## Logging and bug reports

- New or changed features must log useful lifecycle, configuration, failure and recovery information with a stable subsystem tag. Use `[RTX]` for RTX bridge, NR, FG, FSR reconstruction, runtime validation and worker diagnostics; add a subtag such as `[RTX][Runtime]`, `[RTX][NR]` or `[RTX][FG]` when helpful. Use `[EyeCandy]`, `[Screenshot]`, `[App]` or another appropriate tag for other features.
- Preserve timestamps and severity. Keep every line of a multiline diagnostic identifiable. Buffer worker stderr until complete lines arrive; never mix diagnostics into its binary stdout protocol. Bound buffers and repeated logs; log state changes instead of every frame.
- Log actionable reasons and fallback behavior. Include relevant non-secret configuration, runtime identity and process exit details. Redact credentials and personal identifiers before console/session/persistent output. Retain third-party logs in their original format rather than rewriting vendored code for tags.
- Keep README bug-report instructions, issue templates and log/crash headers synchronized with the real filenames and locations. Explain which main, session and supplementary logs to attach, and keep crash dumps private. Do not upload reports automatically.
- Verify changed logging paths with focused checks, including redaction, partial/multiline worker output and crash reporting where applicable. Keep local reports, captures, runtime binaries and handoff journals outside source commits.

## Local release workflow

- Keep one active local build at `release/ngx`; preserve `release/original` as the reference build. Use `scripts/Build-ReleaseCandidate.ps1` to rebuild the configured GUI/worker and replace that folder after staging succeeds. The root launcher must point there.
- Do not create timestamped release directories or ZIP archives for routine builds. Create an archive only when the user asks. Temporary staging/rollback folders belong under the ignored build directory and are removed after success.
- A supplied, manifest-matching NR DLL may be included in `release/ngx/runtimes` for local testing. Keep all runtime DLLs and build output ignored; public packaging continues to omit NR by default. Preserve local SM86 diagnostic logs across rebuilds.
- Do not terminate a running user app to replace or clean its release. Validate resolved cleanup paths and refuse links or paths outside the intended build/release roots before deleting or moving directories.
