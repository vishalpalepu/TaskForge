#pragma once

#include "taskforge/scheduler/process_executor.hpp"

#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace taskforge::testing {

/**
 * @brief In-memory test double for IProcessExecutor.
 *
 * Records all dispatches and allows tests to configure deterministic canned responses,
 * error outcomes, or exception simulations without touching the host operating system.
 */
class FakeProcessExecutor : public scheduler::IProcessExecutor {
public:
    using Handler = std::function<scheduler::ExecutionResponse(const scheduler::ExecutionRequest&)>;

    FakeProcessExecutor() = default;

    /// @brief Sets a default response returned when no specific executable matches.
    void set_default_response(scheduler::ExecutionResponse response) {
        default_response_ = std::move(response);
    }

    /// @brief Configures a canned response for a specific executable path.
    void set_response_for(const std::filesystem::path& executable, scheduler::ExecutionResponse response) {
        responses_.insert_or_assign(executable.string(), std::move(response));
    }

    /// @brief Configures a dynamic handler for flexible assertions or stateful behaviors.
    void set_handler(Handler handler) {
        custom_handler_ = std::move(handler);
    }

    /// @brief Simulates an executor exception during execution.
    void set_throw_on_execute(bool should_throw, std::string message = "Simulated executor failure") {
        throw_on_execute_ = should_throw;
        throw_message_ = std::move(message);
    }

    scheduler::ExecutionResponse execute(const scheduler::ExecutionRequest& request) override {
        recorded_requests_.push_back(request);

        if (throw_on_execute_) {
            throw std::runtime_error(throw_message_);
        }

        if (custom_handler_) {
            return custom_handler_(request);
        }

        auto it = responses_.find(request.executable.string());
        if (it != responses_.end()) {
            return it->second;
        }

        return default_response_;
    }

    [[nodiscard]] const std::vector<scheduler::ExecutionRequest>& recorded_requests() const noexcept {
        return recorded_requests_;
    }

    [[nodiscard]] size_t execution_count() const noexcept {
        return recorded_requests_.size();
    }

    void clear() {
        recorded_requests_.clear();
        responses_.clear();
        custom_handler_ = nullptr;
        throw_on_execute_ = false;
        default_response_ = scheduler::ExecutionResponse::completed(0, "", "", std::chrono::milliseconds{0});
    }

private:
    scheduler::ExecutionResponse default_response_{
        scheduler::ExecutionResponse::completed(0, "", "", std::chrono::milliseconds{0})};
    std::map<std::string, scheduler::ExecutionResponse> responses_;
    Handler custom_handler_{nullptr};
    std::vector<scheduler::ExecutionRequest> recorded_requests_;
    bool throw_on_execute_{false};
    std::string throw_message_{"Simulated executor failure"};
};

} // namespace taskforge::testing
