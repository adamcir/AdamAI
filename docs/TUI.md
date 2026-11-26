# Adam AI terminal interface

Adam AI includes an **original ncursesw TUI** with a conversation-first layout,
visually inspired by OpenCode, Codex CLI, and Claude Code. No code or graphical
assets from those products were copied.

## Layout

- Compact top bar for Adam AI name, model, and generation status.
- Centered chat transcript, capped at 94 terminal columns for readability.
- Visually separated user panels, assistant messages, collapsed reasoning,
  simple Markdown block rendering (headings, bullets, quotes, fenced code).
- A small welcome screen for a fresh conversation.
- Fixed bottom composer with three visible editable lines. Use **Ctrl+O** to
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
| Ctrl+L | New conversation (when not generating) |
| Ctrl+C | Cancel the current Ollama request |
| Ctrl+Q | Exit |
| F1 | Show keyboard help |
| PgUp/PgDn or mouse wheel | Scroll conversation |
| Left/Right, Home/End, Backspace/Delete | Edit text |
| Ctrl+A / Ctrl+E / Ctrl+U | Go to start/end of input, or clear input |
| Escape | Close a popup |

Commands: `/help`, `/new`, `/clear`, `/model NAME`, `/thinking`, `/sidebar`,
`/theme`, `/status`, `/quit`.

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
It does not run shell commands, mutate files or save sessions to disk.
Markdown is intentionally basic and not a full CommonMark parser.
The editor shows three rows at a time; it scrolls to follow the cursor.
Sessions and prompt history exist only during the current process lifetime.
