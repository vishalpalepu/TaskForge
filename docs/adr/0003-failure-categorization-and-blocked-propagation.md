# ADR 0003: Failure Categorization and Blocked Dependent Propagation

## Status
Accepted

## Context
When a task fails, downstream dependent tasks must not execute. However, simply marking tasks as "failed" without preserving context causes major usability and diagnostic issues:
1. Conflating **Process Launch Failures** (e.g. binary not found, invalid path, permission denied) with **Process Non-Zero Exits** (e.g. compiler error, test assertion failure). In launch failures, no OS process ever ran, so fabricating an exit code is misleading.
2. Conflating **Active Failures** with **Blocked Prerequisite Aborts**. If Task $B$ fails and downstream Task $C$ never runs, marking Task $C$ as "failed" confuses users into thinking Task $C$'s command was executed and encountered an error.

## Decision
1. Introduce an explicit `FailureCategory` enumeration:
   - `None`: Success, blocked, or skipped.
   - `LaunchError`: OS failed to create child process. Exit code is `std::nullopt`; Win32 error code (`GetLastError()`) is preserved.
   - `NonZeroExit`: Process spawned and executed, but returned a non-zero exit code. Exit code is preserved.
   - `Timeout`: Process exceeded configured execution duration.
   - `SystemFault`: Win32 pipe or IO failure during execution.
2. Introduce an explicit `TaskState::Blocked` state:
   - A task is marked `Blocked` if and only if one or more of its prerequisites entered `Failed` or `Blocked`.
   - Blocked tasks are never launched, and no child processes are created.
   - Independent DAG branches continue executing unaffected.
3. Both `LaunchError` and `NonZeroExit` transition the executing task to `TaskState::Failed`, which then triggers downstream dependents to transition to `TaskState::Blocked`.

## Consequences
- **Positive**: Clear, actionable error reports distinguishing system environment issues from code/test failures; zero unnecessary process launches; independent branches complete safely.
- **Negative**: Requires maintaining two separate fields in `TaskResult` (`exit_code` and `os_error_code`), along with conditional formatting in CLI reporters.
