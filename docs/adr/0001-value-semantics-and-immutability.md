# ADR 0001: Value Semantics and Invariant Protection for Task and Workflow Definitions

## Status
Accepted

## Context
In workflow orchestration, tasks and workflows represent core domain configurations. We must decide how to model these domain entities to preserve their invariants while maintaining idiomatic Modern C++ performance and safety.

A common anti-pattern in C++ is declaring member variables `const` inside a class (e.g., `const TaskId id_;`). While this guarantees immutability, it severely degrades value semantics:
1. It deletes default move-assignment (`operator=(Type&&)`) and copy-assignment.
2. It disables `std::swap` and prevents `std::vector` from moving elements during reallocation, forcing expensive deep copies.
3. It prevents using standard algorithms that rely on move-assignability.

Alternatively, some designs introduce an inheritance hierarchy (`class BaseTask`, `class BuildTask : public BaseTask`). This introduces heap allocation indirection, virtual table overhead, and complex pointer ownership without providing any behavioral differentiation.

## Decision
1. `TaskDefinition` and `WorkflowDefinition` are modeled as **encapsulated regular value types with const query APIs**, not polymorphic class hierarchies.
2. Member variables are private, **non-const**, movable types.
3. Invariants are strictly validated during construction via private constructors and static factory functions (`create(...)`).
4. External immutability of domain properties is enforced via the public interface: only `const` accessors are provided, with zero public mutator methods. Declaration ordering coordination is encapsulated through a private friend interface (`friend class WorkflowDefinition`).
5. Classes support standard value operations: copy construction, copy assignment, move construction, and move assignment. Moved-from objects enter a valid but unspecified state and should only be reassigned or destroyed.

## Consequences
- **Positive**: Zero virtual function overhead; cache-friendly contiguous storage in `std::vector<TaskDefinition>`; move operations are efficient ($O(1)$ per-element pointer transfers without heap allocation, meaning vector reallocation across $n$ elements is $\mathcal{O}(n)$ element moves without deep string/vector reallocations); full compatibility with STL containers and algorithms.
- **Negative**: Domain integrity relies on class encapsulation discipline rather than compiler-enforced `const` member variables.
