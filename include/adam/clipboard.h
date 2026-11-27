/* AdamAI - GPL-3.0-only
 * Linux desktop clipboard integration (Wayland/X11).
 */
#ifndef ADAM_CLIPBOARD_H
#define ADAM_CLIPBOARD_H

typedef enum {
    ADAM_CLIPBOARD_UNAVAILABLE = 0,
    ADAM_CLIPBOARD_WAYLAND,
    ADAM_CLIPBOARD_X11
} AdamClipboardBackend;

/* Copy UTF-8 bytes to the OS clipboard, which can then be pasted into another
 * program. Uses only wl-copy, xclip or xsel, launched without a shell.
 * Returns UNAVAILABLE if no working desktop clipboard is found.
 * The in-app clipboard and optional terminal OSC 52 are handled by the TUI.
 */
AdamClipboardBackend adam_clipboard_copy_system(const char *utf8);

#endif
