# TaskForge

> A deterministic, dependency-aware local workflow orchestration engine written in Modern C++20.

TaskForge executes multi-step developer workflows defined as Directed Acyclic Graphs (DAGs), isolates process execution, handles failures predictably, and records execution history.

---

## Features

- **Declarative Workflows**: Define tasks, executable paths, argument lists, working directories, and dependencies cleanly in JSON.
- **Strict Pre-Execution Validation**: Comprehensive validation detects missing required fields, nonexistent dependencies, and circular dependency chains before any command runs.
- **Deterministic Scheduling**: Topological scheduling dispatches ready tasks sequentially (with a planned parallel execution path in later milestones).
- **Graceful Failure Propagation**: When a task fails, dependent tasks are immediately marked as `Blocked`, while independent task branches continue cleanly.
- **Safe Process Invocation**: Direct operating-system process execution (`CreateProcessW` on Windows, POSIX `posix_spawnp`/`exec` on Linux) avoids shell injection vulnerabilities and quoting bugs.
- **Structured Output & History**: Captures exit codes, stdout, stderr, timestamps, and execution durations to a local audit log.

---

## Architecture Overview

TaskForge follows a decoupled, layered architecture:

- **Domain Layer (`taskforge::core`)**: Pure value objects (`TaskId`, `TaskDefinition`, `WorkflowDefinition`, `TaskResult`, `WorkflowExecution`) and deterministic graph algorithms (`DependencyGraph`, `WorkflowValidator`). Zero external dependencies.
- **Orchestration Layer (`taskforge::scheduler`)**: Manages task lifecycle transitions and schedules execution via the `IProcessExecutor` interface.
- **Infrastructure Layer (`taskforge::infra`)**: Contains `SystemProcessExecutor` (platform process spawning), `WorkflowParser` (JSON deserialization via `nlohmann/json`), and `FileHistoryRepository` (local run persistence).
- **Presentation Layer (`taskforge::cli`)**: CLI command parsing (`validate`, `run`, `history`) and formatted terminal reporting.

Detailed documentation is available in the [`docs/`](docs/) directory:
- [Project Vision & Scope](docs/project-vision.md)
- [Domain Model & Lifecycle Invariants](docs/domain-model.md)
- [Architecture & Design Decisions](docs/architecture.md)

---

## Prerequisites & Toolchain Setup

TaskForge requires a C++20 compliant compiler and CMake 3.20+.

### Option A: Windows Native Setup (Recommended for Windows host)
Install a C++ compiler toolchain and CMake via `winget` or Visual Studio Installer:

1. **Install CMake**:
   ```powershell
   winget install Kitware.CMake
   ```
2. **Install a C++ Compiler (Choose one)**:
   - **Visual Studio Build Tools (MSVC)**:
     ```powershell
     winget install Microsoft.VisualStudio.2022.BuildTools --override "--passive --config vs_buildtools.json --add Microsoft.VisualStudio.Workload.VCTools"
     ```
   - **LLVM / Clang**:
     ```powershell
     winget install LLVM.LLVM
     winget install BrechtSanders.WinLibs.POSIX.UCRT.LLVM
     ```
   - **GCC / MinGW-w64**:
     ```powershell
     winget install BrechtSanders.WinLibs.POSIX.UCRT
     ```

### Option B: Windows Subsystem for Linux (WSL2)
If you prefer developing inside Linux:
```bash
wsl -d Ubuntu
sudo apt update
sudo apt install -y build-essential cmake ninja-build git
```

---

## Building TaskForge

Once the toolchain is configured:

```bash
# Configure the build directory
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug

# Compile the project and test suite
cmake --build build --config Debug

# Run the test suite
ctest --test-dir build --output-on-failure
```

---

## Planned CLI Usage

```bash
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
  - Toolchain and repository inspection.
  - Project vision, domain model, ownership model, and testing strategy.
- [ ] **Milestone 1: Domain Model & Workflow Validation**
  - `TaskId`, `TaskDefinition`, `WorkflowDefinition`, `DependencyGraph`, `WorkflowValidator`.
  - JSON parser integration (`nlohmann/json`) and validation test suite.
- [ ] **Milestone 2: Sequential Scheduling Engine**
  - `TaskState` machine, `IProcessExecutor` interface, `FakeProcessExecutor`.
  - Sequential scheduler, failure propagation, and blocked dependent tests.
- [ ] **Milestone 3: Real Process Execution & CLI**
  - OS-specific process spawning (`CreateProcessW` / POSIX), pipe capture.
  - End-to-end command line application (`validate`, `run`).
- [ ] **Milestone 4: Observability & Persistence**
  - Formatted terminal reporting with colorized status and timing.
  - Local history repository and `taskforge history` command.
- [ ] **Milestone 5: Recovery & Reliability**
  - Timeout handling, cancellation propagation, and retry semantics.
- [ ] **Milestone 6: Parallel Execution**
  - Thread-safe multi-worker scheduling for independent DAG branches.
