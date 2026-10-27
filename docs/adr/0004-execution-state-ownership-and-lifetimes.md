# ADR 0004: Execution State Ownership and Lifetime Guarantees

## Status
Accepted

## Context
We evaluated how `WorkflowExecution` runtime state should be owned and managed by `WorkflowScheduler`, and how references to the immutable `WorkflowDefinition` should be maintained during execution.

Two approaches were analyzed for `WorkflowExecution`:
- Option A: Owned via `std::unique_ptr<WorkflowExecution>` on the heap.
- Option B: Managed and returned **directly by value** (`WorkflowExecution`).

Additionally, the scheduler must observe `WorkflowDefinition const&` without risking dangling references.

## Decision
1. **`WorkflowExecution` by Value**:
   - `WorkflowExecution` is a non-polymorphic aggregate of value tables (`TaskState`, `TaskResult`).
   - Stored directly on the stack or local frame within `WorkflowScheduler::run()`.
   - Returned by value via move semantics to the caller (`CliApp`).
   - Avoids heap allocation overhead and unnecessary pointer indirection.
2. **Borrowed Reference Lifetime Guarantee**:
   - `CliApp` parses and owns `WorkflowDefinition definition` on the stack frame of the CLI command handler.
   - `CliApp` invokes `scheduler.run(definition)`.
   - Because `definition` lives in the caller's stack frame enclosing the `run()` call, it is guaranteed to outlive the entire execution of `scheduler.run()`.
   - `WorkflowExecution` holds a borrowed non-owning pointer `const WorkflowDefinition*` solely for the duration of `run()`.
   - When `run()` returns and the execution summary is stored or queried, all necessary identifiers and results are owned by value inside `WorkflowExecution`.

## Consequences
- **Positive**: Eliminates unnecessary heap allocations; improves cache locality; guarantees deterministic lifetime and ownership hierarchies with zero `std::shared_ptr`.
- **Negative**: Callers must ensure `WorkflowDefinition` remains in scope for the duration of the synchronous `run()` invocation.

## Amendment (Milestone 2): Discriminated `WorkflowRunResult` via `std::variant`

In the initial proposal, `WorkflowRunResult` was conceived as a struct containing `ValidationResult validation` and `std::optional<WorkflowExecution> execution`. Unrestricted public fields permitted contradictory combinations (e.g. invalid validation paired with a partial execution).

To enforce internal consistency and mutual exclusivity under C++20 without relying on C++23 `std::expected`:
1. `WorkflowRunResult` encapsulates a discriminated union `std::variant<core::ValidationResult, core::WorkflowExecution>`.
2. Controlled construction is enforced through static factory methods:
   - `WorkflowRunResult::validation_failure(core::ValidationResult)`
   - `WorkflowRunResult::executed(core::WorkflowExecution)`
3. Query methods guarantee type safety and clear state discrimination:
   - `is_valid()` returns `true` if and only if the workflow passed validation and an execution aggregate was produced.
   - `is_successful()` returns `true` if and only if `is_valid()` is `true` and every task completed with `TaskState::Succeeded`.
   - `validation_result()` throws `std::logic_error` if called on a valid run.
   - `execution()` throws `std::logic_error` if called on an invalid run.
   - `execution_ptr()` returns `const WorkflowExecution*` or `nullptr` for non-throwing access.

