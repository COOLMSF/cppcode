#include <CLI/CLI.hpp>
#include <api/ClaudeClient.hpp>
#include <agent/QueryEngine.hpp>
#include <tools/Tool.hpp>
#include <tools/BashTool.hpp>
#include <tools/FileReadTool.hpp>
#include <tools/FileWriteTool.hpp>
#include <tools/FileEditTool.hpp>
#include <tools/GlobTool.hpp>
#include <tools/GrepTool.hpp>
#include <tui/App.hpp>
#include <utils/Config.hpp>
#include <utils/Logger.hpp>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>

// ── Tool registration ─────────────────────────────────────────────────────────
static void registerAllTools(cc::tools::ToolRegistry& reg) {
    reg.registerTool(std::make_shared<cc::tools::BashTool>());
    reg.registerTool(std::make_shared<cc::tools::FileReadTool>());
    reg.registerTool(std::make_shared<cc::tools::FileWriteTool>());
    reg.registerTool(std::make_shared<cc::tools::FileEditTool>());
    reg.registerTool(std::make_shared<cc::tools::GlobTool>());
    reg.registerTool(std::make_shared<cc::tools::GrepTool>());
}

int main(int argc, char** argv) {
    CLI::App cli{"Claude Code (C++) — high-performance AI coding agent"};
    cli.set_version_flag("--version,-v", "1.0.0");

    // ── CLI flags ─────────────────────────────────────────────────────────────
    std::string print_prompt;
    std::string model;
    std::string api_key;
    std::string base_url;
    std::string system_prompt;
    std::string cwd_override;
    std::string dotenv_path;
    int  max_tokens   = 0;
    int  max_turns    = 100;
    bool read_only    = false;
    bool debug_mode   = false;
    bool auto_approve = false;
    bool list_tools   = false;

    cli.add_option("-p,--print", print_prompt,
        "Run in headless/print mode with this prompt (non-interactive)");
    cli.add_option("--model,-m", model,
        "Model name (overrides CLAUDE_MODEL env var)");
    cli.add_option("--api-key", api_key,
        "Anthropic API key (overrides ANTHROPIC_API_KEY env var)");
    cli.add_option("--base-url", base_url,
        "Base URL for the API (overrides ANTHROPIC_BASE_URL env var)");
    cli.add_option("--system-prompt", system_prompt,
        "Custom system prompt");
    cli.add_option("--cwd", cwd_override,
        "Working directory (default: current directory)");
    cli.add_option("--env-file", dotenv_path,
        "Path to .env file (default: .env in cwd or home)");
    cli.add_option("--max-tokens", max_tokens,
        "Maximum output tokens per turn");
    cli.add_option("--max-turns", max_turns,
        "Maximum number of agent turns per conversation");
    cli.add_flag("--read-only", read_only,
        "Disable all file writes and bash commands");
    cli.add_flag("--debug", debug_mode,
        "Enable verbose debug logging");
    cli.add_flag("--auto-approve,-y", auto_approve,
        "Automatically approve permission prompts (use with care)");
    cli.add_flag("--list-tools", list_tools,
        "List available tools and exit");

    CLI11_PARSE(cli, argc, argv);

    // ── Load configuration ────────────────────────────────────────────────────
    auto& cfg = cc::utils::Config::instance();
    cfg.load(dotenv_path.empty() ? "" : dotenv_path);

    // Apply logger settings
    auto& logger = cc::utils::Logger::instance();
    if (debug_mode || cfg.debugMode()) {
        logger.setLevel(cc::utils::LogLevel::Debug);
    }
    if (!print_prompt.empty()) {
        logger.setSilent(true); // don't pollute stdout in print mode
    }

    // Resolve effective config values (CLI > env > default)
    std::string effective_api_key  = api_key.empty()   ? cfg.apiKey()   : api_key;
    std::string effective_base_url = base_url.empty()  ? cfg.baseUrl()  : base_url;
    std::string effective_model    = model.empty()     ? cfg.model()    : model;
    int effective_max_tokens       = max_tokens > 0    ? max_tokens     : cfg.maxTokens();

    // ── Register tools ──────────────────────────────────────────────────────────────
    auto& registry = cc::tools::ToolRegistry::instance();
    registerAllTools(registry);

    if (list_tools) {
        std::cout << "Available tools:\n\n";
        for (const auto& def : registry.definitions()) {
            std::cout << "  " << def.name << "\n"
                      << "    " << def.description << "\n\n";
        }
        return 0;
    }

    if (effective_api_key.empty()) {
        std::cerr << "Error: No API key found.\n"
                  << "Set ANTHROPIC_API_KEY in your environment or .env file,\n"
                  << "or use --api-key <key>.\n";
        return 1;
    }

    // Resolve working directory
    std::filesystem::path cwd = cwd_override.empty()
        ? std::filesystem::current_path()
        : std::filesystem::path(cwd_override);

    if (!std::filesystem::exists(cwd)) {
        std::cerr << "Error: working directory does not exist: " << cwd << "\n";
        return 1;
    }
    cwd = std::filesystem::canonical(cwd);

    // ── Build Claude API client ───────────────────────────────────────────────
    cc::api::ClientConfig client_cfg;
    client_cfg.api_key        = effective_api_key;
    client_cfg.base_url       = effective_base_url;
    client_cfg.default_model  = effective_model;

    auto client = std::make_shared<cc::api::ClaudeClient>(std::move(client_cfg));

    // ── Build tool context ────────────────────────────────────────────────────
    cc::tools::ToolContext tool_ctx;
    tool_ctx.cwd          = cwd;
    tool_ctx.read_only    = read_only;
    tool_ctx.auto_approve = auto_approve;
    tool_ctx.headless     = !print_prompt.empty();
    tool_ctx.askPermission = [](const std::string& msg) {
        std::cerr << "\n[Permission] " << msg << "\nAllow? [y/N] ";
        std::string resp;
        std::getline(std::cin, resp);
        return (!resp.empty() && (resp[0] == 'y' || resp[0] == 'Y'));
    };

    // ── Build query config ────────────────────────────────────────────────────
    cc::agent::QueryConfig query_cfg;
    query_cfg.model         = effective_model;
    query_cfg.max_tokens    = effective_max_tokens;
    query_cfg.max_turns     = max_turns;
    query_cfg.system_prompt = system_prompt;
    query_cfg.auto_approve  = auto_approve;
    query_cfg.headless      = !print_prompt.empty();
    query_cfg.tool_ctx      = tool_ctx;

    // ── Build app config ──────────────────────────────────────────────────────
    cc::tui::AppConfig app_cfg;
    app_cfg.query          = query_cfg;
    app_cfg.initial_prompt = print_prompt.empty() ? "" : "";
    app_cfg.print_mode     = !print_prompt.empty();

    // ── Launch ────────────────────────────────────────────────────────────────
    cc::tui::App app(client, registry, app_cfg);

    if (!print_prompt.empty()) {
        return app.runHeadless(print_prompt);
    } else {
        return app.run();
    }
}
