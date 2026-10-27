# TaskForge: Project Vision & Scope

## 1. Executive Summary

**TaskForge** is a local command-line workflow orchestration engine written in Modern C++20. It enables developers and systems engineers to define, validate, schedule, and execute multi-step directed acyclic graph (DAG) tasks locally with deterministic dependency resolution, process isolation, robust failure handling, and execution history tracking.

The first implementation targets **Windows Native using MSVC and CMake**, executing native Windows development workflows directly on the host machine while maintaining an architectural boundary for future cross-platform support.

---

## 2. Problem Statement

Modern software development involves multi-step build, verification, and packaging processes:
1. Configuring CMake and project dependencies.
2. Compiling binaries with MSVC.
3. Running test suites.
4. Executing static analysis (e.g., `clang-tidy`, `cppcheck`).
5. Generating documentation or packaging build artifacts.

In many development environments, these workflows are managed through:
- **Ad-hoc shell scripts (`.bat`, `.cmd`, `.ps1`)**: Lack dependency awareness, execute sequentially without cycle detection, fail silently if exit codes are not explicitly inspected, and make parallelization or conditional branching difficult.
- **Makefiles / Build Tools**: Focused on file timestamp dependencies rather than task state transitions, process isolation, structured logs, and audit histories.
- **CI/CD Systems (e.g., GitHub Actions, GitLab CI, Jenkins)**: Heavyweight, difficult to replicate faithfully on a developer's local machine, and require cloud infrastructure or containers.

### Why Developers Need TaskForge
Developers need a **lightweight, native, deterministic, dependency-aware task runner** that:
- Runs directly on their local workstation.
- Validates task dependency graphs *before* launching a single process.
- Blocks downstream tasks immediately when a prerequisite fails, while allowing independent branches to continue or cleanly terminate.
- Captures standard output, standard error, exit codes, and timing with microsecond precision.
- Leaves an auditable history of local runs for rapid debugging and post-mortem diagnosis.

---

## 3. Target Users & Personas

| Persona | Primary Needs & Frustrations | TaskForge Value Proposition |
| :--- | :--- | :--- |
| **C++ / Windows Developer** | Wants reliable local build-test-lint-package workflows that match CI behavior without launching heavy Docker containers or waiting for cloud runners. | Fast, native C++ Windows executable that coordinates tasks with zero runtime dependencies. |
| **DevOps / Release Engineer** | Needs consistent task orchestration across developer machines and build agents with clear exit codes and structured JSON reports. | Declarative JSON manifests, strict validation, deterministic exit code semantics, and structured history persistence. |
| **Software Architecture Student / Apprentice** | Wants to understand real-world OOP, RAII, ownership semantics, graph algorithms, and design patterns in Modern C++. | Clean architectural boundaries, explicit ownership model, high testability, and zero premature abstractions. |

---

## 4. Key Use Cases

### Use Case 1: Build & Verification Pipeline
A developer defines a workflow:
`Configure CMake` $\rightarrow$ `Build MSVC` $\rightarrow$ [`Run Tests`, `Run Static Analysis`] $\rightarrow$ `Package Release`.
- If `Build MSVC` fails, both `Run Tests` and `Run Static Analysis` are marked **Blocked** immediately without running.
- If `Run Tests` fails but `Run Static Analysis` succeeds, the overall workflow reports failure, and `Package Release` is blocked.

### Use Case 2: Multi-Branch Independent Tasks
A developer runs two independent pipelines in one workflow:
- Pipeline A: `Fetch Submodules` $\rightarrow$ `Compile LibA`
- Pipeline B: `Generate Docs` $\rightarrow$ `Spellcheck Docs`
- If `Compile LibA` fails, Pipeline B continues unaffected to completion, providing complete diagnostic feedback for the entire run.

### Use Case 3: Offline Validation & Dry Run
Before triggering a long build sequence, the user runs:
```cmd
taskforge validate workflow.json
```
TaskForge checks syntax, verifies all prerequisites exist, detects circular dependencies, and checks executable validity before executing anything.

### Use Case 4: Execution History & Post-Mortem
A developer checks previous workflow executions:
```cmd
taskforge history
```
Reviewing task exit codes, execution duration, and failure reasons from previous runs without re-running time-consuming tasks.

---

## 5. Scope Boundaries and Non-Goals

### In-Scope (MVP through Planned Milestones)
1. **Declarative Task Definitions**: Structured JSON manifests describing tasks, executables, arguments, working directories, and dependencies.
2. **Graph Validation**: Strict DAG validation, non-empty ID checks, duplicate detection, and cycle detection.
3. **Deterministic Sequential Scheduling**: Topological evaluation of ready tasks; strict dependency and failure propagation.
4. **Direct Process Execution**: Safe OS process spawning (`CreateProcessW` on Windows) without shell string concatenation.
5. **Execution State Machine**: Explicit task lifecycles (`Pending`, `Ready`, `Running`, `Succeeded`, `Failed`, `Blocked`).
6. **Output & Artifact Capture**: Capturing stdout, stderr, exit codes, and timestamps.
7. **Local Execution History**: JSON-based journal storing run summaries and task outcomes.
8. **Comprehensive Testing**: Unit tests for graph algorithms and state machines; fake executors for deterministic scheduling validation; integration tests for process spawning.

### Explicit Non-Goals (What TaskForge is NOT)
- **Not a distributed workflow engine** (No Celery, Airflow, Temporal, or multi-node clustering).
- **Not a cloud service or daemon** (No HTTP/REST API, web UI, or background system service).
- **Not a replacement for low-level build systems** (TaskForge orchestrates high-level developer tasks like `cmake --build`, `ctest`, `clang-tidy`; it does not replace CMake, Ninja, or MSVC).
- **Not an arbitrary shell interpreter** (TaskForge executes explicit binaries with argument vectors; it avoids `cmd.exe /c` or `powershell.exe -Command` string interpolation unless explicitly configured, eliminating quoting and injection vulnerabilities).
- **No Unsafe Execution Resume / "Magic Exactly-Once" Claims**: Arbitrary external commands have unmanaged side effects (e.g. file writes, network calls). Reading a historical execution record is an *audit/inspection* capability; TaskForge does not assume external commands are idempotent without explicit configuration.
