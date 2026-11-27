# AdamAI

**AdamAI** is a local AI assistant for Adava Development, written in C17.

Its first user interface is a native **ncursesw terminal chat** with an original
conversation-first layout inspired by OpenCode, Codex CLI, and Claude Code.
It is **not** a fork of those applications and does not include their source code.

## TUI preview (0.1.0-dev)

Default appearance uses the **AdamAI Classic** palette: light green accents,
warm orange for user messages and reasoning, and subtle green borders.
Both dark (default) and light modes share this green/orange identity.

```text
 ◆  AdamAI                           adam1   ● Local
 WORKSPACE / ~/Projects/WORLD-Game
 ─────────────────────────────────────────────────────────

                   ◆  AdamAI
             Your local development assistant

  ┃ YOU
  ┃ Help me fix the renderer in WORLD.
  ┃

  ◆  AdamAI
    ▸ Thinking · Ctrl+T to expand
    First, I would review the existing Vulkan code ...

 ┌───────────────────────────────────────────────────────┐
 │ ❯ Ask AdamAI, or type / for commands...             │
 └───────────────────────────────────────────────────────┘
 Enter send · Ctrl+O newline · Ctrl+P commands
```

This text is an approximate layout, not a screenshot. The actual interface
adapts to terminal width and height and displays a right sidebar on wide terminals.

Features:

- Streamed answers, with separately collapsible reasoning when a model provides it.
- Centered transcript, user panels, simple Markdown blocks and fenced code panels.
- Compact, auto-expanding Unicode composer (1–5 visible rows); `Ctrl+O` inserts a newline, `Enter` sends.
- Slash-command palette (`/` or `Ctrl+P`), F1 shortcut help, prompt history.
- Optional workspace sidebar (`Ctrl+B`) and dark/light themes (`/theme`).
- **Persistent conversations:** auto-save to `~/adam-ai/chats/*.json`, restart recovery,
  chat picker (`Ctrl+K` / `/chats`), rename, manual save and confirmed deletion.
- Asynchronous Ollama API client, cancellation, scrollback and status indicators.
- **Workspace filesystem tools:** `list_directory`, `read_file`, `write_file`
  and `request_workspace`. Model-proposed or manual directory changes require
  an explicit **Y/N approval**, just like writes.
- **Web browsing:** public HTTPS pages, linked pages and public search results
  can be browsed automatically without confirming each URL. Toggle with
  `/web auto`, `/web ask` or `/web off`. Network IP restrictions remain on.
- **Default model:** `adam1` is resolved through Ollama `/api/tags` to an
  installed variant such as `adam1:1.0`. Supports `/model adam1:*`.

### Linux dependencies

```bash
sudo apt install build-essential cmake ninja-build \
  libncurses-dev libcurl4-openssl-dev libjson-c-dev pkg-config
```

### Build and test

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

### Run

Start Ollama locally. AdamAI fetches `/api/tags` to choose an installed
model whose name is `adam1` or starts with `adam1:`, for example
`adam1:1.0`. The highest numeric version is preferred:

```bash
ollama list
./build/AdamAI
```

You can also select the family with `/model adam1:*` or an explicit tag with
`/model adam1:1.0`. An older saved chat retains its historical model name,
except that a bare `adam1` will be resolved automatically. New chats use the
auto-selected Adam 1 model. Start Ollama before launching AdamAI.

Or try a lighter model:

```bash
ollama pull qwen3:8b
ADAM_MODEL=qwen3:8b ADAM_THINK=true ./build/AdamAI
```

| Environment variable | Default | Description |
|---|---|---|
| `ADAM_MODEL` | `adam1` | Automatically resolves installed `adam1:*` tags |
| `ADAM_OLLAMA_URL` | `http://127.0.0.1:11434` | Local Ollama endpoint |
| `ADAM_THINK` | `medium` | `low`, `medium`, `high`, `true` or `off`, model-dependent |
| `ADAM_TUI_THEME` | `dark` | `dark` or `light` |
| `ADAM_WEB_ENABLED` | `1` | Read-only web browsing on by default |
| `ADAM_WEB_CONFIRM` | `0` | Set `1` to confirm every outbound web URL |
| `ADAM_FS_ENABLED` | `1` | FS tools enabled by default; set `0` to disable |
| `ADAM_FS_ROOT` | Current directory | Explicit project root (defaults to where AdamAI is launched) |

For key bindings, architecture and limitations see [docs/TUI.md](docs/TUI.md).

### Current limitations

This preview includes chat, saved sessions, read-only public web browsing,
search, and scoped filesystem/developer tools. It cannot run JavaScript in
pages or browse authenticated websites; search can be blocked by providers.
Chat histories remain local (not sent to GitHub). Internet requests can contact
external servers when web mode is enabled or a one-off URL is provided.
Do not confuse the application's local *workspace label* with filesystem access.

## License

GNU GPL-3.0-only. See [LICENSE](LICENSE).

### Saved chats

AdamAI automatically creates `~/adam-ai/chats/` with private directory permissions.
Each conversation is stored in its own UTF-8 JSON file (including the selected model
and reasoning output). A write is first committed to a private temporary file and
atomically renamed, so interrupted writes do not truncate the previous save.

- `Ctrl+K` or `/chats`: browse chats, arrows to select, Enter to open, D to delete
  (confirmation with Y), Esc to close.
- `Ctrl+L` or `/new`: new chat; the previous chat remains saved.
- `/rename TITLE`: rename the current chat.
- `/save`: force a save.
- `/delete`: open the deletion confirmation for the current chat.

The most recently modified chat is restored when AdamAI starts. Saving happens
after the prompt, periodically during streaming and when a response ends, is
cancelled, or the program exits normally. A power loss can still lose the very
last streamed characters. The initial empty chat is saved only once used or when
`/save` is requested. No automatic cloud sync is performed.

### Web access (first implementation)

Public web browsing is **enabled by default** and automatic: AdamAI can
search and visit publicly accessible HTTPS pages, then follow HTTPS links
returned from those pages. `/web auto` enables automatic read-only browsing;
`/web ask` requires Y/N confirmation for each requested URL; `/web off`
disables browsing. `/web https://example.com/` requests a specific URL.
The active model must support Ollama function calling (tools).

**Privacy:** When web and FS are enabled simultaneously, the model may have
access to local file contents, and an external URL or search query can reveal
those contents. Automatic browsing does **not** prevent this kind of leakage.
For sensitive work, use `/fs off` before browsing, or `/web ask` to review
the entire destination URL before each request. URLs may be recorded by
visited websites. Never use private credentials in URLs.

The web tool only reads text, HTML or JSON over verified **HTTPS** on port 443.
It rejects private/reserved destination IPs (including after DNS resolution),
follows up to 3 HTTPS-only redirects, uses no proxies or local credentials,
extracts up to 24 browsable HTTPS links, and limits downloads to 512 KiB,
text to 12,000 bytes, and requests to 20 seconds.
At most twelve tool-call rounds occur per AI response. JavaScript-driven
sites, login-required pages and binary files are not supported. Public web
search is best-effort and may be blocked by automated-traffic protections. Web content is untrusted and may contain errors or instructions;
AdamAI forwards it as reference data, not as instructions to follow.

A browsing action happens in a background Ollama request worker; `Ctrl+C`
cancels it. Visited URLs are visible in the conversation, but fetched page
bodies are not saved to chat history. External websites can see network requests
and the URL you/model supplied.

### Filesystem tools (first implementation)

FS tools are now **enabled by default** for the current working directory.
Start AdamAI from your project root, or set `ADAM_FS_ROOT` to an absolute
directory. To switch projects during a session, press `Ctrl+W` or type
`/workspace /absolute/path`. A **Y/N approval dialog** must be accepted before
the active project changes. The model may likewise call
`request_workspace("/absolute/path")`, which always requires permission.

`/fs off` disables file tools for future requests, and `/fs on` re-enables them.

```bash
cd ~/Dokumenty/Projekty/C-C++/WORLD-Game
/path/to/AdamAI/build/AdamAI
# In AdamAI: /fs on
```

Ask AdamAI to list files, read a C source file, or propose a modification.
`list_directory(path)` and `read_file(path)` are read-only.
`write_file(path, content)` requires **explicit approval every time**:
the ncurses dialog displays the complete proposed content; scroll with
PgUp/PgDn or arrows and press `Y` to approve, `N` or Esc to reject.
There is no shell tool and no automatic write approval. The write replaces the
entire file, so review it carefully. A denied write is reported to the model.

Paths are relative to the project, cannot use `..` or absolute paths and
cannot traverse symbolic links. Common secret locations (.git, .env, .ssh,
.aws) are blocked. Only UTF-8 text files are supported (read limit 128 KiB,
write limit 32 KiB). Writes use a temporary file and atomic rename, preserving
the existing file's mode; no new parent directories are created.
**Filesystem and web tools are both enabled at startup.** Public HTTPS
browsing is now automatic. This is convenient but allows data in model-generated
URLs or search queries to reach outside servers. Use `/web ask` (or
`ADAM_WEB_CONFIRM=1`) for approval before every outbound URL, or `/fs off`
while browsing sensitive projects.

This is defense in depth, **not a security sandbox**. The Ollama model still
receives local file contents when using FS tools. Disable permissions when
working with sensitive data or on an untrusted project.

The initial workspace is the process working directory, unless
`ADAM_FS_ROOT` is set. `/workspace` changes the approved project for subsequent
model requests (and updates the workspace header/sidebar) without changing the
application process working directory. Filesystem access happens on your own PC through
the local application, not through ChatGPT or GitHub.

## Developer agent tools (new)

AdamAI's C17 tool registry exposes **32 tools** (run
`/tools` in the TUI to see the categories):

- **Files/code:** `find_files`, `search_files`, `find_symbol`,
  `find_references`, `project_index`, `inspect_dependencies`,
  `edit_file`, `apply_patch`, `create_directory`, `move_file`,
  `delete_file`. Search is bounded (depth 4, up to 512 entries, 64 matches);
  source symbols are literal string searches rather than a full LSP.
  `apply_patch` uses exact `old_text`/`new_text` replacement, **not**
  unified-diff patch parsing.
- **Terminal and builds:** `execute_command`, `build_project`,
  `run_tests`, `get_diagnostics`, `format_code`. Commands use an
  argument vector (no shell expansion), selected executable allowlist,
  30-second timeout, bounded output, a minimal environment, and child process
  limits. **Every executable command requires a separate Y/N approval**,
  except explicit read-only Git inspection and the model list. Formatting also requires approval.
  This is **NOT a secure OS sandbox**: approved build scripts and compilers
  can execute arbitrary code with the current user's privileges. Only grant
  approval to code you trust.
- **Git/system:** `git_status`, `git_diff`, `git_log`, `git_branch`,
  `git_commit`, `hardware_info`, `model_manager`. `git_commit`
  commits only already-staged changes after approval.
  `format_code` prints formatting rather than writing to disk.

Source: `include/adam/tool_registry.h` and `src/agent/tool_registry.c`.
The existing Ollama backend registers the tool JSON schemas and routes calls
through this shared module. FS permission must be on for developer tools.
No arbitrary shell, sudo, unfenced background jobs, or automatic command approval.

**Additional tools now included:** `memory_search`, `memory_update`
(user-approved), `task_manager` (add, list and complete local tasks),
`web_search` (experimental public HTML search; some search engines may block
automated clients), `web_extract`, `http_request` (HTTPS GET only),
`github_issues`, `github_pull_request` and `github_actions`
(read-only public GitHub API). Network tools show the complete destination URL
for explicit user approval whenever used from the developer agent. Notes and
tasks are private files under `~/adam-ai/`.

**Not implemented in this stage:** full LSP-based symbol resolution,
persistent process sessions, first-class GitHub issue/PR mutations,
JavaScript browser control, configurable authenticated API calls, downloads,
long-term memory and task indexing, model training/manipulation, and full
filesystem sandboxing. These require additional provider/backend integration,
credential handling or stronger isolation.
