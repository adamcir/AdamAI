# TUI architecture

- `main.c`: locale initialization, starts TUI.
- `src/ui/tui.c`: **single-threaded** ncursesw UI, display layout, composer and key handling.
- `src/core/chat.c`: UI-independent conversation history and text buffers.
- `src/backends/ollama.c`: asynchronous HTTP streaming, NDJSON parsing, synchronized queue.
- UI thread polls events every 60 ms; the worker thread never calls curses functions.
- `ADAM_STREAM_THINKING` is displayed separately from `ADAM_STREAM_TEXT`.
- `ADAM_STREAM_ERROR` is surfaced in the transcript. `Ctrl+C` cancels network work.
- This implementation is an **original layout inspired by OpenCode** rather than a code port.

Next: abstract backend interface, tool call events, chat persistence, async job status,
command palette, mouse scrolling, richer message formatting, external editor.
