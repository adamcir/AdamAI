# Adam AI

Local AI assistant by Adava Development. Version **0.1.0-dev**.

The first interface is an original **C17/ncursesw TUI inspired by OpenCode's terminal workflow**. This implementation does not reuse OpenCode's source code. It currently connects to local Ollama and streams answer tokens and, where supported, reasoning updates.

## Build (Ubuntu)

```bash
sudo apt install build-essential cmake ninja-build pkg-config libncurses-dev libcurl4-openssl-dev libjson-c-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run

Start Ollama locally and pull a model, for example:

```bash
ollama pull gpt-oss:20b
./build/adam
```

For an alternative model:

```bash
ADAM_MODEL=qwen3:8b ADAM_THINK=off ./build/adam
```

Environment variables: `ADAM_MODEL` (default `gpt-oss:20b`), `ADAM_OLLAMA_URL` (default `http://127.0.0.1:11434`), `ADAM_THINK` (`off|low|medium|high`, default `medium`), `ADAM_CTX` (default 8192).

## Controls

| Shortcut | Action |
| --- | --- |
| Enter | Send message |
| Left/Right, Home/End | Edit prompt |
| Up/Down | Prompt history |
| PageUp/PageDown, mouse wheel | Scroll conversation |
| Ctrl+T | Expand/collapse streamed reasoning |
| Ctrl+C | Cancel current generation; otherwise clear prompt |
| Ctrl+D | Exit when idle |
| /help | List commands |
| /clear | Clear conversation |
| /model MODEL | Change local Ollama model |
| /think LEVEL | Set reasoning level (`off`, `low`, `medium`, `high`) |
| /quit | Exit |

## Implementation notes

- Network I/O runs on a worker thread; all ncurses calls run on the main thread.
- Ollama NDJSON is parsed incrementally with safe buffering; `thinking` and `content` are displayed separately.
- The first release intentionally does not execute shell commands or access project files. File and command tools require a separate permission system.
- Tool calls, Markdown rendering, persistent chats, GTK GUI and local GGUF/libllama support are future milestones.
- The default backend assumes a trusted local Ollama server; don't bind a server with access to private projects to the public internet without authentication.

Licensed under **GNU GPL-3.0-only**; see `LICENSE`.
