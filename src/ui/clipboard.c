/* AdamAI - GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "adam/clipboard.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CLIPBOARD_MAX_BYTES (64u * 1024u)
#define CLIPBOARD_TIMEOUT_MS 3000

static int64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

extern char **environ;

static bool copy_to_process(const char *const argv[], const char *text, size_t length) {
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) return false;
    /* posix_spawnp avoids calling fork()+execvp() from a multithreaded
     * ncurses/Ollama application. File actions map the socket to stdin. */
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(pair[0]); close(pair[1]);
        return false;
    }
    int err = posix_spawn_file_actions_adddup2(&actions, pair[1], STDIN_FILENO);
    if (!err) err = posix_spawn_file_actions_addclose(&actions, pair[0]);
    if (!err) err = posix_spawn_file_actions_addclose(&actions, pair[1]);
    if (!err) err = posix_spawn_file_actions_addopen(
        &actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    if (!err) err = posix_spawn_file_actions_addopen(
        &actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid = -1;
    if (!err) err = posix_spawnp(&pid, argv[0], &actions, NULL,
                                 (char *const *)argv, environ);
    (void)posix_spawn_file_actions_destroy(&actions);
    if (err) {
        close(pair[0]); close(pair[1]);
        return false;
    }

    close(pair[1]);
    int flags = fcntl(pair[0], F_GETFL);
    if (flags >= 0) (void)fcntl(pair[0], F_SETFL, flags | O_NONBLOCK);

    int64_t start = monotonic_ms();
    bool complete = start >= 0;
    size_t written = 0;
    while (complete && written < length) {
        int64_t elapsed = monotonic_ms() - start;
        if (elapsed < 0 || elapsed >= CLIPBOARD_TIMEOUT_MS) { complete = false; break; }
        ssize_t n = send(pair[0], text + written, length - written, MSG_NOSIGNAL);
        if (n > 0) { written += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd fd = { .fd = pair[0], .events = POLLOUT };
            int ms = CLIPBOARD_TIMEOUT_MS - (int)elapsed;
            int ready = poll(&fd, 1, ms);
            if (ready > 0 && (fd.revents & POLLOUT)) continue;
        }
        complete = false;
    }
    close(pair[0]); /* EOF tells the clipboard program it has all text. */

    int status = 0;
    bool exited = false;
    while (start >= 0 && monotonic_ms() - start < CLIPBOARD_TIMEOUT_MS) {
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) { exited = true; break; }
        if (ret < 0) break;
        struct timespec nap = { .tv_sec = 0, .tv_nsec = 10000000L };
        (void)nanosleep(&nap, NULL);
    }
    if (!exited) {
        (void)kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
    return complete && exited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

AdamClipboardBackend adam_clipboard_copy_system(const char *utf8) {
    if (!utf8) return ADAM_CLIPBOARD_UNAVAILABLE;
    size_t bytes = strlen(utf8);
    if (!bytes || bytes > CLIPBOARD_MAX_BYTES) return ADAM_CLIPBOARD_UNAVAILABLE;

    if (getenv("WAYLAND_DISPLAY") && *getenv("WAYLAND_DISPLAY")) {
        const char *const wl[] = { "wl-copy", "--type", "text/plain;charset=utf-8", NULL };
        if (copy_to_process(wl, utf8, bytes)) return ADAM_CLIPBOARD_WAYLAND;
    }
    if (getenv("DISPLAY") && *getenv("DISPLAY")) {
        const char *const xc[] = { "xclip", "-selection", "clipboard", "-in", NULL };
        if (copy_to_process(xc, utf8, bytes)) return ADAM_CLIPBOARD_X11;
        const char *const xs[] = { "xsel", "--clipboard", "--input", NULL };
        if (copy_to_process(xs, utf8, bytes)) return ADAM_CLIPBOARD_X11;
    }
    return ADAM_CLIPBOARD_UNAVAILABLE;
}
