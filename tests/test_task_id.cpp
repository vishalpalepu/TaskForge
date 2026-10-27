#include "taskforge/core/task_id.hpp"

#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <unordered_set>

using namespace taskforge::core;

TEST_CASE("TaskId validation and construction", "[domain][task_id]") {
    SECTION("Valid task IDs are accepted") {
        REQUIRE(TaskId::is_valid("build"));
        REQUIRE(TaskId::is_valid("compile-engine"));
        REQUIRE(TaskId::is_valid("test_suite_123"));
        REQUIRE(TaskId::is_valid("package.release.v1"));
        REQUIRE(TaskId::is_valid("A"));

        TaskId id("build-all_steps.1");
        CHECK(id.value() == "build-all_steps.1");
        CHECK(id.view() == "build-all_steps.1");
    }

    SECTION("Invalid task IDs are rejected") {
        REQUIRE_FALSE(TaskId::is_valid(""));
        REQUIRE_FALSE(TaskId::is_valid("task with spaces"));
        REQUIRE_FALSE(TaskId::is_valid("task/with/slashes"));
        REQUIRE_FALSE(TaskId::is_valid("task\\with\\backslashes"));
        REQUIRE_FALSE(TaskId::is_valid("task:colon"));
        REQUIRE_FALSE(TaskId::is_valid("task@special"));
        REQUIRE_FALSE(TaskId::is_valid("task#hash"));
    }

    SECTION("Constructor throws std::invalid_argument on invalid input") {
        REQUIRE_THROWS_AS(TaskId(""), std::invalid_argument);
        REQUIRE_THROWS_AS(TaskId("invalid name"), std::invalid_argument);
        REQUIRE_THROWS_AS(TaskId("foo/bar"), std::invalid_argument);
    }

    SECTION("try_create returns std::nullopt on invalid input") {
        CHECK_FALSE(TaskId::try_create("").has_value());
        CHECK_FALSE(TaskId::try_create("bad id!").has_value());

        auto id_opt = TaskId::try_create("good-id");
        REQUIRE(id_opt.has_value());
        CHECK(id_opt->value() == "good-id");
    }

    SECTION("Equality and three-way comparison (C++20 spaceship)") {
        TaskId a("task-a");
        TaskId b("task-b");
        TaskId a_duplicate("task-a");

        CHECK(a == a_duplicate);
        CHECK(a != b);
        CHECK(a < b);
        CHECK(b > a);
        CHECK((a <=> b) < 0);
    }

    SECTION("Hash support in std::unordered_set") {
        std::unordered_set<TaskId> set;
        set.insert(TaskId("task-1"));
        set.insert(TaskId("task-2"));
        set.insert(TaskId("task-1")); // Duplicate

        CHECK(set.size() == 2);
        CHECK(set.contains(TaskId("task-1")));
        CHECK(set.contains(TaskId("task-2")));
        CHECK_FALSE(set.contains(TaskId("task-3")));
    }

    SECTION("Stream insertion operator") {
        TaskId id("format-check");
        std::ostringstream oss;
        oss << id;
        CHECK(oss.str() == "format-check");
    }

    SECTION("Moved-from contract: safe reassignment and destruction") {
        TaskId a("task-a");
        TaskId b = std::move(a);
        CHECK(b.value() == "task-a");

        // Reassigning moved-from object is safe
        a = TaskId("task-reassigned");
        CHECK(a.value() == "task-reassigned");
    }
}
