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
 ◆  AdamAI                           gpt-oss:20b   ● Local
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
- **Opt-in filesystem tools:** `list_directory`, `read_file` and `write_file`
  operate within the open project. Every write waits for a **Y/N approval dialog**
  that shows the complete proposed file content. Enable with `/fs on`.
- **Opt-in web tool:** Ollama models supporting function calling can request
  `web_fetch` to read public HTTPS pages. Use `/web on`, `/web off`,
  or explicitly `/web https://example.com/` for a one-off visit.

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

Start Ollama locally, download a compatible model, then:

```bash
ollama pull gpt-oss:20b
./build/AdamAI
```

Or try a lighter model:

```bash
ollama pull qwen3:8b
ADAM_MODEL=qwen3:8b ADAM_THINK=true ./build/AdamAI
```

| Environment variable | Default | Description |
|---|---|---|
| `ADAM_MODEL` | `gpt-oss:20b` | Ollama model name |
| `ADAM_OLLAMA_URL` | `http://127.0.0.1:11434` | Local Ollama endpoint |
| `ADAM_THINK` | `medium` | `low`, `medium`, `high`, `true` or `off`, model-dependent |
| `ADAM_TUI_THEME` | `dark` | `dark` or `light` |
| `ADAM_WEB_ENABLED` | `0` | Set `1` for opt-in web access at startup |
| `ADAM_FS_ENABLED` | `0` | Set `1` to enable project filesystem tools at startup |
| `ADAM_FS_ROOT` | Current directory | Explicit project root (defaults to where AdamAI is launched) |

For key bindings, architecture and limitations see [docs/TUI.md](docs/TUI.md).

### Current limitations

This preview provides chat, saved sessions, the **read-only web_fetch**
internet tool and scoped **filesystem tools**. It still cannot run shell
commands, browse JavaScript websites or search the web automatically.
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

The internet tool is **off by default**. Type `/web on` and ask AdamAI to read
a specific URL, or use `/web https://example.com/` for a single fetch without
turning on automatic browsing. Type `/web off` to disable future requests.
The active model must support Ollama function calls (tools).

The web tool only reads text, HTML or JSON over verified **HTTPS** on port 443.
It rejects private/reserved destination IPs (including after DNS resolution),
does not follow redirects, uses no proxies or local credentials, and limits
downloads to 512 KiB, extracted text to 12,000 bytes, and requests to 20 seconds.
At most two tool-call rounds occur per AI response. JavaScript-driven sites,
search queries, login-required pages, binary files and redirected URLs are not
yet supported. Web content is untrusted and may contain errors or instructions;
AdamAI forwards it as reference data, not as instructions to follow.

A browsing action happens in a background Ollama request worker; `Ctrl+C`
cancels it. Visited URLs are visible in the conversation, but fetched page
bodies are not saved to chat history. External websites can see network requests
and the URL you/model supplied.

### Filesystem tools (first implementation)

To work with a project, start AdamAI **from its root directory** and type
`/fs on`. This grants the model access to the project root for the remainder of
the session. `/fs off` disables file tools for future requests.

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
**Filesystem and web tools cannot be enabled together in one request**
to reduce accidental data exfiltration. Neither tool is enabled by default.
This is a defense-in-depth boundary, not a fully isolated sandbox: do not
enable it in untrusted project directories or expose the TUI to unknown users.

The selected workspace is the process working directory, unless
`ADAM_FS_ROOT` is set. Filesystem access happens on your own PC through
the local application, not through ChatGPT or GitHub.
