# TaskForge

> A deterministic, dependency-aware local workflow orchestration engine written in Modern C++20 for Windows.

TaskForge executes multi-step developer workflows defined as Directed Acyclic Graphs (DAGs), isolates process execution, handles failures predictably, and records execution history.

---

## Features

- **Declarative Workflows**: Define tasks, executable paths, argument lists, working directories, and dependencies cleanly in JSON.
- **Strict Pre-Execution Validation**: Comprehensive validation detects missing required fields, nonexistent dependencies, and circular dependency chains before any command runs.
- **Deterministic Sequential Scheduling**: Topological scheduling dispatches ready tasks sequentially (with a planned parallel execution path in later milestones).
- **Graceful Failure & Blocked Propagation**: When a task fails, dependent tasks are immediately marked as `Blocked`, while independent task branches continue cleanly to completion.
- **Safe Native Windows Process Execution**: Direct Win32 process execution (`CreateProcessW`) avoids shell injection vulnerabilities and quoting bugs.
- **Structured Output & History**: Captures exit codes, stdout, stderr, timestamps, and execution durations to a local audit log.

---

## Architecture & Core Distinctions

TaskForge enforces strict boundaries across its layers:

- **Domain Layer (`taskforge::core`)**: Pure value objects (`TaskId`, `TaskDefinition`, `WorkflowDefinition`, `TaskResult`, `WorkflowExecution`) and deterministic graph algorithms (`DependencyGraph`, `WorkflowValidator`). Zero external dependencies.
- **Orchestration Layer (`taskforge::scheduler`)**: Manages task lifecycle transitions and schedules execution via the abstract `IProcessExecutor` interface.
- **Infrastructure Layer (`taskforge::infra`)**: Contains `WindowsProcessExecutor` (native Win32 process spawning and pipe redirection), `WorkflowParser` (JSON deserialization via `nlohmann/json`), and `FileHistoryRepository` (local run persistence).
- **Presentation Layer (`taskforge::cli`)**: CLI command parsing (`validate`, `run`, `history`) and formatted terminal reporting.

Key architectural distinctions:
1. **Workflow Definitions vs. Execution Instances**: Immutable design-time specifications vs. ephemeral runtime session states.
2. **Task Identity vs. Task Ownership**: Lightweight `TaskId` value tokens vs. vector-owned `TaskDefinition` containers.
3. **Dependencies vs. Ownership**: Tasks reference each other only via IDs in a graph; tasks never hold pointers to other tasks.
4. **Policy vs. Mechanism**: Scheduler evaluates readiness; `IProcessExecutor` spawns processes. Tested via `FakeProcessExecutor`.
5. **Failed vs. Blocked**: A task is `Failed` if its process returned non-zero; it is `Blocked` if it was aborted because a prerequisite failed.
6. **Audit Replay vs. Safe Repeat**: History files provide read-only post-mortems; external commands are not assumed to be idempotent.

Detailed documentation is available in the [`docs/`](docs/) directory:
- [Project Vision & Scope](docs/project-vision.md)
- [Domain Model & Lifecycle Invariants](docs/domain-model.md)
- [Architecture & Design Decisions](docs/architecture.md)

---

## Prerequisites & Windows Native Toolchain Setup

TaskForge requires a C++20 compliant MSVC compiler and CMake 3.20+.

### Required Setup
To install CMake and Visual Studio Build Tools via `winget` on Windows, run the following in an elevated PowerShell:

```powershell
# 1. Install CMake
winget install Kitware.CMake

# 2. Install Visual Studio 2022 Build Tools (MSVC C++ Workload)
winget install Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Once installed, build tasks should be run from the **Developer PowerShell for VS 2022** or after initializing `vcvars64.bat`.

---

## Building TaskForge

```cmd
# Configure the build directory
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug

# Compile the project and test suite
cmake --build build --config Debug

# Run the test suite
ctest --test-dir build --output-on-failure
```

---

## Planned CLI Usage

```cmd
# Validate a workflow definition without executing commands
taskforge validate ./workflows/build_and_test.json

# Execute a workflow and produce an execution summary
taskforge run ./workflows/build_and_test.json

# Inspect previous execution runs
taskforge history
```

---

## Implementation Roadmap

- [x] **Milestone 0: Reconnaissance & Architectural Design**
  - Toolchain and repository forensic inspection.
  - Project vision, domain model, ownership model, and testing strategy.
- [ ] **Milestone 1: Domain Model & Workflow Validation**
  - `TaskId`, `TaskDefinition`, `WorkflowDefinition`, `DependencyGraph`, `WorkflowValidator`.
  - JSON parser integration (`nlohmann/json`) and validation test suite.
- [ ] **Milestone 2: Sequential Scheduling Engine**
  - `TaskState` machine, `IProcessExecutor` interface, `FakeProcessExecutor`.
  - Sequential scheduler, failure propagation, and blocked dependent tests.
- [ ] **Milestone 3: Windows Process Execution & CLI**
  - Native Win32 process spawning (`CreateProcessW`), pipe capture, RAII handle wrappers.
  - End-to-end command line application (`validate`, `run`).
- [ ] **Milestone 4: Observability & Persistence**
  - Formatted terminal reporting with colorized status and timing.
  - Local history repository and `taskforge history` command.
- [ ] **Milestone 5: Recovery & Reliability**
  - Timeout handling, cancellation propagation, and retry semantics.
- [ ] **Milestone 6: Parallel Execution**
  - Thread-safe multi-worker scheduling for independent DAG branches.
