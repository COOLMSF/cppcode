# cc-haha-cpp

A high-performance C++20 rewrite of the [cc-haha](https://github.com/NanmiCoder/cc-haha) AI coding agent (itself based on Claude Code).

## Why C++?

| Metric | TypeScript/Bun | C++ |
|--------|----------------|-----|
| Cold-start time | ~500 ms JIT warmup | <10 ms native |
| Memory baseline | ~150 MB V8 heap | ~8 MB |
| Tool execution overhead | event-loop scheduling | zero-copy POSIX |
| Binary distribution | requires Bun runtime | single static binary |

## Features

- **Streaming API client** — libcurl SSE parser with zero-copy event dispatch
- **Identical tool set** — `Bash`, `Read`, `Write`, `Edit`, `Glob`, `Grep`
- **FTXUI terminal UI** — reactive, component-based TUI (same feel as React + Ink)
- **Headless / `--print` mode** — pipe-friendly non-interactive output
- **Any compatible API** — OpenRouter, MiniMax, local proxies via `ANTHROPIC_BASE_URL`
- **Zero runtime deps** — single binary after `cmake --build`

## Build

### Prerequisites

```bash
# macOS
brew install cmake curl

# Ubuntu / Debian
apt install cmake libcurl4-openssl-dev
```

CMake ≥ 3.25 and a C++20 compiler (Clang 14+ or GCC 12+) are required.  
All other dependencies (nlohmann/json, CLI11, FTXUI) are fetched automatically by CMake FetchContent.

### Compile

```bash
git clone <this-repo> cc-haha-cpp
cd cc-haha-cpp

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Binary is at:
./build/claude-haha --help
```

### Install globally (optional)

```bash
cmake --install build --prefix ~/.local
# Then add ~/.local/bin to your PATH
```

## Configuration

```bash
cp .env.example .env
# Edit .env and set ANTHROPIC_API_KEY
```

The agent reads configuration in priority order:

1. CLI flags (`--api-key`, `--model`, …)
2. Environment variables (`ANTHROPIC_API_KEY`, `CLAUDE_MODEL`, …)
3. `.env` file in the current directory
4. `.env` file in `$HOME`

### Key environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `ANTHROPIC_API_KEY` | — | **Required.** Your API key |
| `ANTHROPIC_BASE_URL` | `https://api.anthropic.com` | Custom API endpoint |
| `CLAUDE_MODEL` | `claude-opus-4-5` | Model name |
| `CLAUDE_MAX_TOKENS` | `8096` | Max output tokens per turn |
| `CLAUDE_DEBUG` | `0` | Enable debug logging |

## Usage

```bash
# Interactive TUI
./build/claude-haha

# Headless / print mode (pipe-friendly)
./build/claude-haha -p "Explain the main function in src/main.cpp"

# Custom model / endpoint
./build/claude-haha --model claude-sonnet-4-5 --base-url https://openrouter.ai/api/v1

# Read-only mode (no file writes or shell commands)
./build/claude-haha --read-only

# List available tools
./build/claude-haha --list-tools

# Override working directory
./build/claude-haha --cwd /path/to/project
```

## Architecture

```
cc-haha-cpp/
├── include/
│   ├── api/
│   │   ├── Types.hpp         # Message, ContentBlock, StreamEvent types
│   │   └── ClaudeClient.hpp  # libcurl streaming client
│   ├── agent/
│   │   └── QueryEngine.hpp   # Main agentic loop (tool-use ↔ API)
│   ├── tools/
│   │   ├── Tool.hpp          # Abstract base + ToolRegistry
│   │   ├── BashTool.hpp      # Shell command execution (fork/exec + poll)
│   │   ├── FileReadTool.hpp  # File/directory reading with line numbers
│   │   ├── FileWriteTool.hpp # Atomic file creation / overwrite
│   │   ├── FileEditTool.hpp  # Exact-string replacement edits
│   │   ├── GlobTool.hpp      # Recursive glob with ** support
│   │   └── GrepTool.hpp      # Regex / fixed-string search
│   ├── tui/
│   │   └── App.hpp           # FTXUI interactive TUI + headless mode
│   └── utils/
│       ├── Config.hpp        # .env + env-var config loader
│       ├── Logger.hpp        # Thread-safe logger
│       └── StringUtils.hpp   # String helpers + ANSI colours
└── src/                      # Implementations mirror include/
```

### Adding a new tool

1. Create `include/tools/MyTool.hpp` extending `cc::tools::Tool`
2. Create `src/tools/MyTool.cpp` implementing `name()`, `description()`, `inputSchema()`, `execute()`
3. Add `src/tools/MyTool.cpp` to `CMakeLists.txt`
4. Call `registry.registerTool(std::make_shared<MyTool>())` in `src/main.cpp`

## Differences from cc-haha (TypeScript)

| Feature | cc-haha (TS) | cc-haha-cpp |
|---------|-------------|-------------|
| Runtime | Bun + V8 | Native binary |
| TUI | React + Ink | FTXUI |
| MCP servers | ✓ | Planned |
| Multi-agent | ✓ | Planned |
| Memory system | ✓ | Planned |
| Channel system | ✓ | Planned |
| Computer Use | ✓ | Planned |
| Skills system | ✓ | Planned |

## License

Based on Claude Code source code © Anthropic. For educational and research use only.
