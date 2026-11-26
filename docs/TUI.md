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
  `/web HTTPS_URL`: one-shot explicit web fetch. `ADAM_WEB_ENABLED=1`
  enables web tools at process launch. Off by default.
- Protection against local network targets includes rejection of private and
  loopback IPs even if a public DNS record resolves to one. No redirects, no
  proxy, no local credentials, no POST, no filesystem access.
- `tests/test_web_fetch.c` checks URL/IP protections and HTML extraction.
  `tests/test_ollama_web_loop.c` with `tests/mock_ollama.py` checks the
  full two-turn Ollama tool protocol without calling the public Internet.
- Limitations: no search engine, browser JavaScript, cookies/login, following
  redirects, or automatic verification of what a page claims. Model-generated
  tool calls are limited to two rounds per user turn. Some model builds cannot
  emit Ollama-compatible function calls; in that case use a supported model.
