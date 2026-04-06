#include <agent/QueryEngine.hpp>
#include <api/ClaudeClient.hpp>
#include <utils/Logger.hpp>
#include <utils/StringUtils.hpp>
#include <chrono>
#include <sstream>
#include <ctime>
#include <iomanip>

namespace cc::agent {

QueryEngine::QueryEngine(
    std::shared_ptr<cc::api::ClaudeClient> client,
    tools::ToolRegistry& registry
) : client_(std::move(client)), registry_(registry) {}

void QueryEngine::abort() { aborted_.store(true); }
bool QueryEngine::isAborted() const { return aborted_.load(); }

void QueryEngine::emit(const AgentEvent& ev) {
    if (on_event_) on_event_(ev);
}

std::string QueryEngine::buildSystemPrompt(const QueryConfig& cfg) const {
    if (!cfg.system_prompt.empty()) return cfg.system_prompt;

    // Build a default system prompt similar to the original
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream date_ss;
    date_ss << std::put_time(&tm_buf, "%A, %B %d, %Y");

    std::string cwd_str = cfg.tool_ctx.cwd.string();

    return
        "You are Claude Code, an AI coding assistant running as a command-line tool.\n\n"
        "Current date: " + date_ss.str() + "\n"
        "Working directory: " + cwd_str + "\n\n"
        "You have access to tools for reading and writing files, running bash commands, "
        "searching code, and browsing the web. Use them to help the user with their tasks.\n\n"
        "Guidelines:\n"
        "- Be concise and practical. Prefer actions over lengthy explanations.\n"
        "- When making file edits, use the Edit tool for targeted changes and Write for new files.\n"
        "- Always read files before editing them.\n"
        "- Run tests after making changes when possible.\n"
        "- If a task is ambiguous, ask a clarifying question before proceeding.\n"
        "- Respect the working directory; use relative paths when referring to project files.\n";
}

std::string QueryEngine::runTurn(
    std::vector<api::Message>& conversation,
    const std::string& user_input,
    const QueryConfig& config
) {
    aborted_.store(false);

    // Add user message to conversation
    conversation.push_back(api::Message::userText(user_input));

    std::string accumulated_text;
    int turn = 0;

    while (turn < config.max_turns && !isAborted()) {
        ++turn;
        emit(ev::RequestStart{});

        // Build request
        api::ApiRequest req;
        req.model      = config.model;
        req.max_tokens = config.max_tokens;
        req.system     = buildSystemPrompt(config);
        req.messages   = conversation;
        req.stream     = true;

        // Add tool definitions (filtered if allowed_tools is specified)
        auto all_defs = registry_.definitions();
        for (const auto& def : all_defs) {
            if (config.allowed_tools.empty()) {
                req.tools.push_back(def);
            } else {
                for (const auto& allowed : config.allowed_tools) {
                    if (def.name == allowed) { req.tools.push_back(def); break; }
                }
            }
        }

        LOG_INFO("QueryEngine: sending request (turn " + std::to_string(turn) +
                 ", messages=" + std::to_string(conversation.size()) + ")");

        api::ApiResponse response;
        try {
            response = client_->streamRequest(req, [&](const api::StreamEvent& ev) {
                // Forward text deltas to the caller
                if (auto* delta = std::get_if<api::event::ContentBlockDelta>(&ev)) {
                    if (delta->delta_type == "text_delta" && !delta->text.empty()) {
                        accumulated_text += delta->text;
                        emit(ev::TextDelta{ delta->text });
                    }
                }
            });
        } catch (const api::ApiError& e) {
            LOG_ERROR(std::string("API error: ") + e.what());
            emit(ev::Error{ e.what() });
            return accumulated_text;
        } catch (const std::exception& e) {
            LOG_ERROR(std::string("Request failed: ") + e.what());
            emit(ev::Error{ e.what() });
            return accumulated_text;
        }

        if (isAborted()) break;

        // Add assistant response to conversation
        api::Message assistant_msg;
        assistant_msg.role    = api::Role::Assistant;
        assistant_msg.content = response.content;
        conversation.push_back(assistant_msg);

        // Emit turn complete
        emit(ev::TurnComplete{ response.stop_reason, response.usage });

        // If stop reason is not tool_use, we're done
        if (response.stop_reason != api::StopReason::ToolUse) {
            break;
        }

        // Execute all tool calls
        auto tool_results = executeTools(response.content, config);
        if (isAborted()) break;

        // Add tool results as user messages
        for (auto& tr : tool_results) {
            conversation.push_back(std::move(tr));
        }
    }

    if (turn >= config.max_turns) {
        LOG_WARN("QueryEngine: max_turns reached (" + std::to_string(config.max_turns) + ")");
    }

    return accumulated_text;
}

std::vector<api::Message> QueryEngine::executeTools(
    const std::vector<api::ContentBlock>& content,
    const QueryConfig& config
) {
    std::vector<api::Message> results;

    for (const auto& block : content) {
        if (isAborted()) break;

        const auto* tu = std::get_if<api::ToolUseBlock>(&block);
        if (!tu) continue;

        // Emit tool start
        std::string input_preview;
        try { input_preview = tu->input.dump().substr(0, 200); } catch (...) {}
        emit(ev::ToolStart{ tu->id, tu->name, input_preview });

        LOG_INFO("Executing tool: " + tu->name + " (id=" + tu->id + ")");

        tools::ToolCallResult tool_result;

        auto tool = registry_.findTool(tu->name);
        if (!tool) {
            tool_result = tools::ToolCallResult::error(
                "Unknown tool: " + tu->name + ". Available tools: " +
                [&]() {
                    std::string names;
                    for (const auto& d : registry_.definitions()) {
                        if (!names.empty()) names += ", ";
                        names += d.name;
                    }
                    return names;
                }()
            );
        } else {
            std::string validation_err = tool->validateInput(tu->input);
            if (!validation_err.empty()) {
                tool_result = tools::ToolCallResult::error("Invalid input: " + validation_err);
            } else {
                try {
                    tool_result = tool->execute(tu->input, config.tool_ctx);
                } catch (const std::exception& e) {
                    tool_result = tools::ToolCallResult::error(
                        std::string("Tool execution failed: ") + e.what()
                    );
                }
            }
        }

        // Truncate oversized results
        constexpr size_t MAX_RESULT = 200 * 1024;
        if (tool_result.content.size() > MAX_RESULT) {
            tool_result.content = tool_result.content.substr(0, MAX_RESULT) +
                "\n\n... (output truncated to 200KB)";
        }

        emit(ev::ToolEnd{
            tu->id, tu->name,
            utils::truncate(tool_result.content, 300),
            tool_result.is_error
        });

        // Build tool result message
        api::Message result_msg;
        result_msg.role = api::Role::User;
        result_msg.content.push_back(api::ToolResultBlock{
            tu->id,
            tool_result.content,
            tool_result.is_error
        });
        results.push_back(std::move(result_msg));
    }

    return results;
}

} // namespace cc::agent
