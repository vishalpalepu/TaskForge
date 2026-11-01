# ADR 0005: Win32 Process Execution, Restrictive Handle Whitelisting, and Output Draining

## Status
Accepted

## Context
Milestone 2 introduced the abstract `IProcessExecutor` boundary to isolate task scheduling from process execution. Milestone 3 Stage 3B introduces the concrete production implementation for Windows: `WindowsProcessExecutor`.

Process execution on Windows presents specific failure modes:
1. **Search Path Vulnerability**: Calling `CreateProcessW` without `lpApplicationName` or relying on unconstrained `SearchPathW` allows implicit execution of binaries from the working directory.
2. **Handle Leakage**: Setting `bInheritHandles = TRUE` with standard `STARTUPINFOW` leaks all inheritable parent handles into the child, preventing pipe EOF detection and risking file-lock conflicts.
3. **Pipe Buffer Deadlock**: Standard Win32 anonymous pipes have finite kernel buffers (typically 64 KiB). Reading stdout and stderr sequentially deadlocks if the child fills the unread stream buffer.
4. **Shell Script Inconsistency**: Direct invocation of `.bat` or `.cmd` files without an explicit shell interpreter creates security vulnerabilities and command-line parsing discrepancies.
5. **Command-Line Parsing Discrepancies**: Windows passes a raw command-line string. Child applications tokenize arguments independently according to their runtime startup code.

## Decision
1. **Targeted Microsoft UCRT Quoting**:
   - The quoting algorithm specifically targets the Microsoft Universal C Runtime (UCRT / MSVCRT / `CommandLineToArgvW`) parsing standard. Compatibility with non-CRT or custom parsers is not claimed.
   - All quoting operations occur on UTF-16 wide strings after validating UTF-8 input.
   - Backslashes preceding quotes are doubled ($2N + 1$); trailing backslashes at the end of a quoted argument are doubled ($2N$) followed by a plain closing quote (`"`).
   - Command lines exceeding the Windows limit of 32,767 characters are rejected.
2. **Deterministic Executable Resolution**:
   - `request.executable` is resolved prior to calling `CreateProcessW`.
   - Explicit paths are canonicalized against the process working directory and verified; bare names are resolved strictly by enumerating sanitized environment `PATH` directories in order, explicitly skipping empty entries and the current working directory.
   - Lookup precedence prioritizes `name.exe` over unextended names.
   - All extension comparisons are case-insensitive.
   - The resolved absolute path is passed explicitly as `lpApplicationName`.
   - Direct execution of `.cmd` and `.bat` files is rejected with `ERROR_BAD_EXE_FORMAT`.
3. **Restrictive Handle Whitelisting (`STARTUPINFOEXW`)**:
   - Uses `STARTUPINFOEXW` with `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`.
   - Exactly three standard handles are inherited: `hStdInput` (redirected to `NUL`), `hStdOutput` (pipe write end), and `hStdError` (pipe write end). All other parent handles are strictly excluded.
   - Parent copies of the write handles are closed immediately after `CreateProcessW` returns.
4. **Pre-Launch Reader Threads & Fatal Error Coordinator Protocol**:
   - Both reader threads are spawned before `CreateProcessW` is called. If thread creation fails, no child process is ever spawned, and cleanup is trivial.
   - Standard output and standard error are drained concurrently by the reader threads to eliminate pipe buffer saturation deadlocks.
   - In-memory output capture is bounded to 10 MiB per stream; excess bytes are discarded with a truncation warning to protect against memory exhaustion while allowing the child process to finish.
   - A Win32 manual-reset event (`hFatalErrorEvent`) allows readers to signal fatal errors to the coordinator thread. If signaled, the coordinator terminates the direct child (`TerminateProcess`), unblocks the pipes, and joins both threads cleanly before returning `system_fault`.
   - No joinable thread can reach destruction; `std::terminate` is strictly prevented.
5. **Orphaned Descendants Boundary**:
   - Child processes are expected to be cooperative tasks. Process-tree containment (via Windows Job Objects) and timeouts are deferred to Milestone 6.

## Consequences
- **Positive**: Immune to pipe buffer saturation deadlocks; immune to parent handle leakage; immune to current-directory binary hijacking; guaranteed thread safety and resource cleanup via RAII; deterministic argument quoting for UCRT-based executables.
- **Negative**: Direct execution of batch files (`.cmd`, `.bat`) requires explicit `cmd.exe /c` task definition. Uncooperative processes that launch background daemons retaining pipe handles remain a documented limitation until Milestone 6.
