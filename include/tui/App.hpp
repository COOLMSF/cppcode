#pragma once
#include <agent/QueryEngine.hpp>
#include <api/ClaudeClient.hpp>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <deque>
#include <thread>

namespace cc::tui {

using namespace cc::api;
using namespace cc::agent;

// A single rendered chat entry shown in the conversation pane
struct ChatEntry {
    enum class Kind { User, AssistantText, ToolCall, ToolResult, SystemInfo, Error };
    Kind        kind;
    std::string text;
    std::string tool_name;
    bool        is_error = false;
};

// Configuration for the TUI application
struct AppConfig {
    QueryConfig query;
    std::string initial_prompt;   // run this immediately if non-empty
    bool        print_mode = false; // --print / headless
};

// Interactive TUI app built on FTXUI
class App {
public:
    App(
        std::shared_ptr<cc::api::ClaudeClient> client,
        tools::ToolRegistry&                   registry,
        AppConfig                               config
    );
    ~App();

    // Run in interactive TUI mode (blocking)
    int run();

    // Run in headless --print mode (blocking, writes to stdout)
    int runHeadless(const std::string& prompt);

private:
    std::shared_ptr<cc::api::ClaudeClient> client_;
    tools::ToolRegistry&                   registry_;
    AppConfig                              config_;
    agent::QueryEngine                     engine_;

    // Conversation state (shared between TUI thread and worker thread)
    std::vector<Message>         conversation_;
    std::deque<ChatEntry>        chat_log_;
    std::mutex                   log_mu_;

    // TUI state
    std::string   input_buf_;
    std::string   status_line_;
    bool          thinking_  = false;
    std::string   partial_text_; // streaming text being assembled

    // Worker thread management
    std::atomic<bool> worker_running_{false};
    std::atomic<bool> quit_{false};

    // Submit a user turn asynchronously
    void submitTurn(const std::string& user_input);

    // Append a chat entry (thread-safe)
    void addEntry(ChatEntry entry);

    // Handle agent events from the worker thread
    void handleEvent(const AgentEvent& agent_ev);

    // Build the FTXUI component tree
    // (defined in App.cpp, uses FTXUI internals)
    struct FtxuiImpl;
    std::unique_ptr<FtxuiImpl> impl_;
};

} // namespace cc::tui
