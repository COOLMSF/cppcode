#pragma once
#include <api/Types.hpp>
#include <tools/Tool.hpp>
#include <atomic>
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace cc::api { class ClaudeClient; }

namespace cc::agent {

using namespace cc::api;
using namespace cc::tools;

// Events emitted by the agent loop (for the TUI to consume)
namespace ev {

struct TextDelta    { std::string text; };
struct ToolStart    { std::string id; std::string name; std::string input_preview; };
struct ToolEnd      { std::string id; std::string name; std::string result; bool is_error; };
struct TurnComplete { StopReason stop_reason; Usage usage; };
struct Error        { std::string message; };
struct RequestStart {};   // a new API call is being made

} // namespace ev

using AgentEvent = std::variant<
    ev::TextDelta,
    ev::ToolStart,
    ev::ToolEnd,
    ev::TurnComplete,
    ev::Error,
    ev::RequestStart
>;

using AgentEventCallback = std::function<void(const AgentEvent&)>;

// Configuration for the query engine
struct QueryConfig {
    std::string               model;
    int                       max_tokens     = 8096;
    int                       max_turns      = 100;
    std::string               system_prompt;
    std::vector<std::string>  allowed_tools; // empty = all tools allowed
    bool                      auto_approve   = false;
    bool                      headless       = false;
    ToolContext                tool_ctx;
};

// The main agent loop. Run one user turn (including all tool-use sub-turns).
class QueryEngine {
public:
    QueryEngine(
        std::shared_ptr<cc::api::ClaudeClient> client,
        ToolRegistry&                           registry
    );

    // Set callback for agent events (streaming text, tool calls, etc.)
    void setEventCallback(AgentEventCallback cb) { on_event_ = std::move(cb); }

    // Run a single user turn. May call Claude multiple times (tool loop).
    // Returns the final assistant text response (accumulated).
    std::string runTurn(
        std::vector<Message>&   conversation,
        const std::string&      user_input,
        const QueryConfig&      config
    );

    // Abort an in-progress turn (thread-safe)
    void abort();
    bool isAborted() const;

private:
    std::shared_ptr<cc::api::ClaudeClient> client_;
    ToolRegistry&                           registry_;
    AgentEventCallback                      on_event_;
    std::atomic<bool>                       aborted_{false};

    // Execute all tool calls in a response, return tool result messages
    std::vector<Message> executeTools(
        const std::vector<ContentBlock>& content,
        const QueryConfig&               config
    );

    void emit(const AgentEvent& ev);

    // Build system prompt (with CWD, date, etc.)
    std::string buildSystemPrompt(const QueryConfig& config) const;
};

} // namespace cc::agent
