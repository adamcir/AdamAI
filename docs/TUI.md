# AdamAI terminal interface

AdamAI includes an **original ncursesw TUI** with a conversation-first layout,
visually inspired by OpenCode, Codex CLI, and Claude Code. No code or graphical
assets from those products were copied.

## Palette

The **AdamAI Classic** theme uses light green for branding and active states,
orange for user messages and reasoning, muted green for borders, and neutral
text backgrounds. The palette also has a light variant and an 8-color fallback.

## Layout

- Compact top bar for AdamAI name, model, and generation status.
- Centered chat transcript, capped at 94 terminal columns for readability.
- Visually separated user panels, assistant messages, collapsed reasoning,
  simple Markdown block rendering (headings, bullets, quotes, fenced code).
- A small welcome screen for a fresh conversation.
- Compact bottom composer that grows from one to five visible editable lines. Use **Ctrl+O** to
  insert a newline and **Enter** to send. Unicode text is supported.
- Optional right sidebar showing workspace, model, session, and shortcuts.
  It is shown by default on terminals at least 108 columns wide.
- Slash-command palette with keyboard navigation and a toggleable F1 help overlay.
- Dark 256-color theme by default; a light theme can be selected with `/theme`
  or `ADAM_TUI_THEME=light`. There is an 8-color fallback.

## Controls

| Keys | Action |
|---|---|
| Enter | Send, or choose the highlighted slash command |
| Ctrl+O | Newline in the composer |
| Ctrl+P | Open or toggle slash-command suggestions |
| Up/Down | Choose a slash command, or browse prior prompts |
| Ctrl+B | Toggle sidebar |
| Ctrl+T | Expand/collapse reasoning returned by the model |
| Ctrl+L | New saved conversation (when not generating) |
| Ctrl+K | Browse saved conversations |
| Ctrl+C | Cancel the current Ollama request |
| Ctrl+Q | Exit |
| F1 | Show keyboard help |
| PgUp/PgDn or mouse wheel | Scroll conversation |
| Left/Right, Home/End, Backspace/Delete | Edit text |
| Ctrl+A / Ctrl+E / Ctrl+U | Go to start/end of input, or clear input |
| Escape | Close a popup |

Commands: `/help`, `/new`, `/clear`, `/chats`, `/save`, `/rename TITLE`,
`/delete`, `/model NAME`, `/thinking`, `/sidebar`, `/theme`, `/status`, `/quit`.

## Implementation

- `main.c` starts the TUI and initializes the process locale.
- `src/ui/tui.c` is the only module that calls ncursesw, on the main thread.
- `src/core/chat.c` owns the UTF-8 chat history independently of the UI.
- `src/backends/ollama.c` streams Ollama NDJSON over libcurl in a worker thread.
  It writes to a mutex-protected queue; the UI polls the queue every 60 ms.
- Thinking and content are separate stream channels. Some models don't expose
  thinking at all; the application does not fabricate it.
- `/model` starts a fresh conversation to avoid mixing incompatible context.
- The sidebar's 'Agent tools: coming soon' line is descriptive, not an active tool.

## Known limitations

This version is a **chat client**, not yet a filesystem or internet agent.
It does not run shell commands or modify project files. Chat histories are persisted
locally under `~/adam-ai/chats/`.
Markdown is intentionally basic and not a full CommonMark parser.
The editor displays up to five rows; overflowing messages scroll to follow the cursor.
Saved chats survive a restart; the editable prompt history is process-local.

## Word-aware composer layout

- `include/adam/editor_layout.h` and `src/ui/editor_layout.c` compute **one shared** row/terminal-cell map for the editor and its insertion caret.
- Soft wraps occur at word boundaries when possible; a word longer than the available width is split only as a last resort.
- Whitespace suppressed at a visual line break remains unchanged in the original message sent to the model.
- Explicit newlines are respected. Unicode glyph widths are calculated with `wcwidth` in the active UTF-8 locale.
- The editor remains compact until additional rows are needed (up to 5 visible rows) and keeps the cursor in view when editing longer prompts.
- `tests/test_editor_layout.c` covers the reported Czech `Lorem Ipsum` wrapping regression, exact boundaries, explicit newlines and full-width Unicode.

## Persistent conversations

- `include/adam/chat_store.h` and `src/core/chat_store.c` implement JSON persistence
  (json-c) independently of curses.
- Directory: `~/adam-ai/chats/`. Directory creation uses mode 0700, files 0600.
- Each chat has a stable filename ID, user-editable title, last-used model, updated
  timestamp, and its user/assistant messages with content and optional reasoning.
- Only IDs consisting of ASCII letters, numbers, `-` and `_` are accepted in file
  operations; path traversal and symlink file loading are rejected.
- Saves are written via a temporary file + fsync + rename. The old file remains
  intact if a write fails.
- On launch, the most recently modified saved chat is automatically loaded.
  The in-TUI picker supports opening and deleting with confirmation.
- The active chat is checkpointed after sending a prompt, every ~2 seconds while
  streaming, on completion/cancellation and before switching or exiting.
- Conversation list is limited to 128 most recent entries in this preview.
  Simultaneous AdamAI processes do not yet implement per-chat write locking.

## Web agent tool (initial stage)

- `src/tools/web_fetch.c`, `include/adam/web_fetch.h`: HTTPS GET with
  URL validation, remote-IP checks before sending the request, TLS validation,
  connection limits, cancellation and basic HTML-to-text conversion.
- `src/backends/ollama.c`: registers the `web_fetch(url)` JSON tool when
  enabled, reads tool calls in Ollama's streaming NDJSON response, executes them
  on the existing worker thread, appends the `role:tool` result to the next
  Ollama request and streams the final response back to ncurses.
- `/web on` / `/web off`: toggle tool availability for subsequent model turns.
  `/web HTTPS_URL`: one-shot explicit web fetch. Web tools are enabled at
  startup; `/web off` disables them, `/web auto` allows automatic reading,
  and `/web ask` requests per-URL confirmation.
- Protection against local network targets includes rejection of private and
  loopback IPs even if a public DNS record resolves to one. HTTPS redirects
  are limited to three hops. No proxy, local credentials, POST or binary downloads.
- `tests/test_web_fetch.c` checks URL/IP protections and HTML extraction.
  `tests/test_ollama_web_loop.c` with `tests/mock_ollama.py` checks the
  full two-turn Ollama tool protocol without calling the public Internet.
- Limitations: public HTML search may be blocked by search providers; no
  browser JavaScript, cookies/login or automatic verification of page claims.
  Model-generated tool calls are limited to twelve rounds per user turn. Some model builds cannot
  emit Ollama-compatible function calls; in that case use a supported model.

## Workspace filesystem agent tools (initial version)

- `src/tools/fs_tool.c` uses directory file descriptors and `openat` with
  `O_NOFOLLOW` to keep operations inside the chosen project root. Absolute
  paths, `..` and symlink traversal are rejected. Common secret locations
  are blocked by default.
- `list_directory(path)`: up to 128 entries per directory, directories/files/
  symlinks distinguished, no recursive traversal.
- `read_file(path)`: UTF-8 text only, 128 KiB maximum.
- `write_file(path, content)`: UTF-8 text only, 32 KiB maximum. Replaces an
  entire file or creates it if its parent directory exists. A blocking
  approval gate waits for a **real Y/N keypress in the TUI** before writing.
  The complete proposal is scrollable, and denial is returned as a tool result.
  Writes use an atomic temporary-file rename with fsync.
- Enable with `/fs on`, disable with `/fs off`, check with `/fs status`.
  Enabled by default; `ADAM_FS_ENABLED=0` disables it at startup. Root
  defaults to current directory; `ADAM_FS_ROOT` can override it.
- Security: FS and read-only public web tools may be active together. This can
  expose file text through model-generated URLs or queries. For sensitive
  projects, disable FS while browsing or choose `/web ask`.
- Bounded search, terminal commands, exact-text patches and Git inspection
  are available in the developer tool registry. File changes and process
  execution still require explicit user approval.

## Default model, both tool modes and workspace approval

- Default Ollama model: `adam1` (override via `ADAM_MODEL`). A reopened saved
  conversation retains its previously selected model.
- Web and FS tools start **enabled**, unless `ADAM_WEB_ENABLED=0` or
  `ADAM_FS_ENABLED=0` is set. Both can now be active in the same model turn.
- Public HTTPS requests run automatically by default, including when FS is
  active. `/web ask` or `ADAM_WEB_CONFIRM=1` adds per-URL Y/N approval.
  With both tool classes active, review the security implications; automatic
  browsing is not a data-loss prevention sandbox.
- `/workspace /absolute/project/path` or `Ctrl+W` requests access to an
  existing project folder. The dialog displays both old and new roots. Only Y
  changes the authorized root; N/Esc retains the previous workspace.
- The model can also call `request_workspace(path)` with an **absolute** path.
  It cannot switch the directory itself; the model worker blocks until local
  user approval. On success, the sidebar and subsequent tool requests use the
  newly approved root. All writes still require their own separate approval.
- The header/sidebar show the approved FS workspace, not necessarily
  `getcwd()`. Changes to the project do not erase or change saved chats.
- This program does not execute shell commands or silently access arbitrary
  directories; only explicitly approved workspace directories are used.

## C17 developer tool registry

`src/agent/tool_registry.c` provides a separate JSON schema and dispatcher
for 32 source, terminal, Git, web and system tools. `ollama.c` no longer needs
individual branches for new developer tools. Mutating file operations and
process execution use the preexisting modal approval callback. All paths
remain workspace-relative and do not follow symlinks for file operations.

`execute_command` accepts a JSON array `argv` (never shell text). It
executes only known executable names, strips inherited environment variables,
applies process resource limits, captures bounded stdout/stderr and the exit
code, and cancels process groups on timeout. **This is not a secure sandbox.**
An approved build can still run arbitrary project code, access files outside
the selected workspace and use the network. User confirmation is therefore
required every time. For untrusted projects, run AdamAI in an actual container
or an operating-system sandbox.

## More agent tools

The tool registry now includes local memory and task log management plus
read-only public web/GitHub queries. `memory_update` and task mutations
require explicit Y/N approval and write to private local JSONL files under
`~/adam-ai/`. `memory_search` and task listing are read-only.

`web_search` uses a public HTML search interface and is best-effort;
providers may reject automated requests. `http_request` is deliberately
restricted to public HTTPS GET; it does not support POST or account
credentials. GitHub API tools fetch only public data. Outbound calls are
automatic by default; `/web ask` restores per-URL confirmation.

## Automatic web browsing and Adam 1 model discovery

- At launch, AdamAI asks the configured Ollama API for `/api/tags`. An
  untagged default `adam1` or wildcard `adam1:*` is resolved to the
  highest installed numeric tag, such as `adam1:1.0`. Explicit versioned
  model names remain unchanged. The lookup also applies to previously saved
  conversations referencing bare `adam1`.
- Read-only HTTPS requests run without per-page approval by default.
  `/web auto` enables this mode; `/web ask` restores Y/N confirmation
  for each URL; `/web off` disables the tools. `ADAM_WEB_CONFIRM=1` enables
  strict confirmation at startup.
- The HTML extractor includes up to 24 validated HTTPS links, resolves
  relative references, and unwraps common search-result redirect links.
  HTTPS redirects (up to 3) are supported with per-connection public-IP
  checks. The model may search, read an article, then follow links in up to
  12 sequential tool-call rounds per user turn.
- **Security tradeoff:** Automatic requests with FS active may allow model-
  generated search strings/URLs to expose local contents. Public-IP restrictions
  prevent localhost/private-network access, but not data exfiltration to a
  public domain. Use `/web ask` or disable FS for sensitive projects.
- No client-side JavaScript, interactive logins, file uploads, arbitrary
  POST requests or unrestricted binary downloads.
