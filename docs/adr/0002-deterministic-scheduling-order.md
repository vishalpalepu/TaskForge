# ADR 0002: Deterministic Scheduling Order for Independent Tasks

## Status
Accepted

## Context
In a Directed Acyclic Graph (DAG) workflow, multiple tasks may become `Ready` at the same moment (for example, all root tasks with zero prerequisites at the start of execution, or multiple independent branches after a common bottleneck task finishes).

If the scheduler queries ready tasks from a hash table (such as `std::unordered_map` or `std::unordered_set`), the iteration order is non-deterministic. Hash bucket distribution depends on runtime memory layout, hash seeds, compiler versions, and standard library implementations.

Non-deterministic scheduling undermines test reproducibility, complicates debugging, and leads to inconsistent developer experiences across machines.

## Decision
TaskForge implements a strict, two-tiered deterministic tie-breaking policy for the scheduler's ready queue:
1. **Primary Tie-Breaker**: **Declaration Order** in the workflow manifest (`declaration_index`). Tasks declared earlier by the author are scheduled first.
2. **Secondary Tie-Breaker**: **Lexicographical `TaskId`**. If declaration order is ambiguous or missing, tasks are dispatched in alphabetical order of their identifier string.

The ready queue uses an ordered set or sorted queue (`std::set<TaskId, DeterministicOrder>`), guaranteeing identical execution dispatch order across all runs, builds, and platforms.

## Consequences
- **Positive**: 100% reproducible execution sequences; tests can make exact assertions on task dispatch order; predictable developer experience.
- **Negative**: Requires capturing and storing the 0-based `declaration_index` when parsing the manifest into `TaskDefinition`.
