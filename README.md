# Adam AI

Local AI assistant for Adava Development. Written in C17.

## TUI preview (0.1.0-dev)

The first interface is an original **ncursesw** terminal application inspired by
OpenCode's conversation-centered workflow. This is not a fork of OpenCode and
contains no OpenCode source code.

Features:

- A responsive conversation view and optional model/session sidebar.
- Real-time streaming of the assistant answer and available reasoning output.
- Editable Unicode message composer, scrollback and command shortcuts.
- Persistent in-memory conversation context within the current session.
- Asynchronous Ollama API client; terminal remains responsive while generating.
- Terminal size handling, UTF-8 input, errors and cancellation.

### Linux dependencies

```bash
sudo apt install build-essential cmake ninja-build \
  libncurses-dev libcurl4-openssl-dev libjson-c-dev pkg-config
```

### Build

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

Configuration (environment variables):

| Variable | Default | Description |
| --- | --- | --- |
| `ADAM_MODEL` | `gpt-oss:20b` | Ollama model name |
| `ADAM_OLLAMA_URL` | `http://127.0.0.1:11434` | Ollama endpoint, without `/api/chat` |
| `ADAM_THINK` | `medium` | `low`, `medium`, `high`, `true`, or `off`, depending on model |

### Keyboard and commands

| Shortcut | Action |
| --- | --- |
| Enter | Send a message |
| Ctrl+T | Show/hide model reasoning output |
| Ctrl+L | New conversation (while idle) |
| Ctrl+C | Cancel current request |
| Ctrl+Q | Exit |
| PgUp/PgDn | Scroll conversation |
| Left/Right, Home/End, Backspace/Delete | Edit composer |
| Ctrl+A / Ctrl+E / Ctrl+U | Start/end/clear composer |

Commands: `/help`, `/clear`, `/new`, `/thinking`, `/model NAME`, `/quit`.

### Current limitations

This version implements **chat and TUI**, not a computer-control agent.
It does not run shell commands, modify files, browse websites or persist sessions.
Thinking text is displayed only when the chosen model/provider exposes it.
`/model` starts a new conversation because models may use incompatible context.

## License

GNU GPL-3.0-only. See LICENSE.
