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
The editor displays up to five rows; overflowing messages scroll to follow the cursor.
Sessions and prompt history exist only during the current process lifetime.

## Word-aware composer layout

- `include/adam/editor_layout.h` and `src/ui/editor_layout.c` compute **one shared** row/terminal-cell map for the editor and its insertion caret.
- Soft wraps occur at word boundaries when possible; a word longer than the available width is split only as a last resort.
- Whitespace suppressed at a visual line break remains unchanged in the original message sent to the model.
- Explicit newlines are respected. Unicode glyph widths are calculated with `wcwidth` in the active UTF-8 locale.
- The editor remains compact until additional rows are needed (up to 5 visible rows) and keeps the cursor in view when editing longer prompts.
- `tests/test_editor_layout.c` covers the reported Czech `Lorem Ipsum` wrapping regression, exact boundaries, explicit newlines and full-width Unicode.
