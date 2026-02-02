/* server_pty.c -- PTY/Interactive mode for bash-server (Phase 5)

   Copyright (C) 2026 Free Software Foundation, Inc.

   This file is part of GNU Bash, the Bourne Again SHell.

   Bash is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Bash is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Bash.  If not, see <http://www.gnu.org/licenses/>. */

/* PTY mode (architecture §10):
 *
 * Provides full terminal emulation via pseudo-terminal.  When a client
 * sends a "spawn" message on CHAN_PTY (channel 5), the server creates
 * a PTY with forkpty(), execs an interactive bash in the child, and
 * then relays data bidirectionally between the client socket and the
 * PTY master fd using select().
 *
 * Data flow:
 *   Client --[CHAN_PTY: input]--> Server --[write]--> PTY master --> bash
 *   Client <--[CHAN_PTY: output]-- Server <--[read]-- PTY master <-- bash
 *
 * Features:
 *   - Full readline, job control, ANSI escape sequences
 *   - Window resize via TIOCSWINSZ + SIGWINCH
 *   - Signal injection (SIGINT, SIGTSTP, etc.)
 *   - Base64 encoding for binary-safe transfer
 */

#include "server.h"

#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <pty.h>    /* forkpty */
#include <utmp.h>

/* ANSI escape stripping state machine */
enum {
    STRIP_NORMAL = 0,
    STRIP_ESC,
    STRIP_CSI,
    STRIP_OSC,
    STRIP_OSC_ESC,
    STRIP_CHARSET
};

typedef struct ansi_strip_state {
    int state;      /* STRIP_NORMAL, STRIP_ESC, STRIP_CSI, etc. */
    int active;     /* 1 = stripping enabled */
} ansi_strip_state_t;

/* PTY session state */
typedef struct pty_session {
    int    master_fd;      /* PTY master file descriptor */
    pid_t  child_pid;      /* PID of the child bash process */
    int    active;         /* 1 if PTY is running */
    int    rows;           /* Terminal rows */
    int    cols;           /* Terminal cols */
    ansi_strip_state_t strip;  /* ANSI stripping state (if enabled) */
} pty_session_t;

/* ================================================================
 * ANSI escape sequence stripping
 *
 * Stateful filter that removes ANSI escape sequences from a byte
 * stream.  State persists across calls so sequences split across
 * read() boundaries are handled correctly.
 *
 * Handles: CSI (ESC[...), OSC (ESC]...BEL/ST), two-char escapes
 * (ESC + final), charset designators (ESC(/)), 8-bit CSI (0x9B).
 * ================================================================ */

/* Strip ANSI escape sequences from in[0..in_len-1].
   Writes clean bytes to out, returns number of clean bytes written.
   out may alias in (in-place safe since output <= input).
   Not static — called from test_pty.c. */
size_t
ansi_strip(ansi_strip_state_t *st, const char *in, size_t in_len,
           char *out, size_t out_size)
{
    size_t i, o = 0;
    unsigned char c;

    for (i = 0; i < in_len && o < out_size; i++) {
        c = (unsigned char)in[i];

        switch (st->state) {
        case STRIP_NORMAL:
            if (c == 0x1B) {
                st->state = STRIP_ESC;
            } else if (c == 0x9B) {
                /* 8-bit CSI introducer */
                st->state = STRIP_CSI;
            } else {
                out[o++] = (char)c;
            }
            break;

        case STRIP_ESC:
            if (c == '[') {
                st->state = STRIP_CSI;
            } else if (c == ']') {
                st->state = STRIP_OSC;
            } else if (c == '(' || c == ')') {
                st->state = STRIP_CHARSET;
            } else if (c >= 0x40 && c <= 0x7E) {
                /* Two-character escape (ESC + final byte) — consumed */
                st->state = STRIP_NORMAL;
            } else {
                /* Malformed — drop the ESC and this byte */
                st->state = STRIP_NORMAL;
            }
            break;

        case STRIP_CSI:
            /* CSI parameter/intermediate bytes: 0x20-0x3F */
            if (c >= 0x40 && c <= 0x7E) {
                /* Final byte — CSI sequence complete */
                st->state = STRIP_NORMAL;
            }
            /* Else: parameter or intermediate byte, stay in CSI */
            break;

        case STRIP_OSC:
            if (c == 0x07) {
                /* BEL terminates OSC */
                st->state = STRIP_NORMAL;
            } else if (c == 0x1B) {
                /* Possible ST (ESC \) */
                st->state = STRIP_OSC_ESC;
            }
            /* Else: OSC payload byte, stay in OSC */
            break;

        case STRIP_OSC_ESC:
            if (c == '\\') {
                /* ST (String Terminator) — OSC complete */
                st->state = STRIP_NORMAL;
            } else {
                /* Malformed — treat as end of OSC */
                st->state = STRIP_NORMAL;
            }
            break;

        case STRIP_CHARSET:
            /* Consume one byte after ESC( or ESC) */
            st->state = STRIP_NORMAL;
            break;

        default:
            st->state = STRIP_NORMAL;
            out[o++] = (char)c;
            break;
        }
    }

    return o;
}

/* Spawn a PTY session.
 * Creates a pseudo-terminal, forks, and execs bash in the child.
 * Returns 0 on success (pty is populated), -1 on error. */
int
pty_spawn(pty_session_t *pty, int rows, int cols, const char *shell)
{
    struct winsize ws;
    pid_t pid;
    int master;

    if (!shell || !*shell)
        shell = "/bin/bash";

    memset(&ws, 0, sizeof(ws));
    ws.ws_row = (rows > 0) ? rows : 24;
    ws.ws_col = (cols > 0) ? cols : 80;

    pid = forkpty(&master, NULL, NULL, &ws);
    if (pid < 0)
        return -1;

    if (pid == 0) {
        /* Child: exec interactive shell */
        const char *shell_name;

        /* Set TERM if not set */
        if (!getenv("TERM"))
            setenv("TERM", "xterm-256color", 1);

        /* Extract basename for argv[0] (prefix with '-' for login shell) */
        shell_name = strrchr(shell, '/');
        shell_name = shell_name ? shell_name + 1 : shell;

        execlp(shell, shell_name, "--login", "-i", (char *)NULL);
        /* If exec fails */
        perror("exec");
        _exit(127);
    }

    /* Parent */
    pty->master_fd = master;
    pty->child_pid = pid;
    pty->active = 1;
    pty->rows = ws.ws_row;
    pty->cols = ws.ws_col;

    return 0;
}

/* Resize the PTY terminal.
 * Sends TIOCSWINSZ ioctl and SIGWINCH to the child. */
int
pty_resize(pty_session_t *pty, int rows, int cols)
{
    struct winsize ws;

    if (!pty->active)
        return -1;

    memset(&ws, 0, sizeof(ws));
    ws.ws_row = rows;
    ws.ws_col = cols;

    if (ioctl(pty->master_fd, TIOCSWINSZ, &ws) < 0)
        return -1;

    pty->rows = rows;
    pty->cols = cols;

    /* Send SIGWINCH to the process group */
    kill(pty->child_pid, SIGWINCH);

    return 0;
}

/* Send a signal to the PTY child process. */
int
pty_signal(pty_session_t *pty, int signum)
{
    if (!pty->active || pty->child_pid <= 0)
        return -1;

    return kill(pty->child_pid, signum);
}

/* Close the PTY session.
 * Closes the master fd, waits for the child, returns exit code. */
int
pty_close(pty_session_t *pty)
{
    int status = 0;

    if (!pty->active)
        return -1;

    pty->active = 0;

    if (pty->master_fd >= 0) {
        close(pty->master_fd);
        pty->master_fd = -1;
    }

    if (pty->child_pid > 0) {
        /* Wait for child with timeout via polling */
        int i;
        for (i = 0; i < 50; i++) {  /* 5 seconds max */
            if (waitpid(pty->child_pid, &status, WNOHANG) == pty->child_pid)
                goto done;
            usleep(100000);  /* 100ms */
        }
        /* Force kill if still running */
        kill(pty->child_pid, SIGKILL);
        waitpid(pty->child_pid, &status, 0);
    }

done:
    pty->child_pid = -1;

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return -1;
}

/* Run the PTY relay loop.
 *
 * Multiplexes between the client socket (for JSON frames on CHAN_PTY)
 * and the PTY master fd (for terminal I/O).
 *
 * This function takes over the session loop — it reads frames from
 * the client fd and writes PTY input, and reads PTY output and sends
 * it as frames to the client.
 *
 * Returns the child exit code, or -1 on error. */
int
pty_relay_loop(pty_session_t *pty, int client_rfd, int client_wfd)
{
    fd_set rfds;
    int maxfd;
    int done = 0;
    char pty_buf[4096];
    int exit_code = -1;

    while (!done && pty->active) {
        FD_ZERO(&rfds);
        FD_SET(client_rfd, &rfds);
        if (pty->master_fd >= 0)
            FD_SET(pty->master_fd, &rfds);

        maxfd = client_rfd;
        if (pty->master_fd > maxfd)
            maxfd = pty->master_fd;

        /* Use a timeout to periodically check if child is still alive */
        {
            struct timeval tv;
            int ret;
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);
            if (ret < 0) {
                if (errno == EINTR)
                    continue;
                break;  /* Error */
            }
        }

        /* Check for PTY output */
        if (pty->master_fd >= 0 && FD_ISSET(pty->master_fd, &rfds)) {
            ssize_t n = read(pty->master_fd, pty_buf, sizeof(pty_buf));
            if (n > 0) {
                /* Strip ANSI escapes if enabled */
                if (pty->strip.active) {
                    n = ansi_strip(&pty->strip, pty_buf, n,
                                   pty_buf, sizeof(pty_buf));
                    if (n == 0)
                        continue;  /* Entire read was escape sequences */
                }
                /* Base64 encode and send as output frame */
                char *b64 = protocol_base64_encode(pty_buf, n);
                if (b64) {
                    json_frame_write_fmt(client_wfd, CHAN_PTY,
                        "{\"type\":\"output\",\"data\":\"%s\",\"encoding\":\"base64\"}",
                        b64);
                    free(b64);
                }
            } else if (n <= 0) {
                /* PTY closed (child exited) */
                pty->active = 0;
                done = 1;
            }
        }

        /* Check for client input (JSON frames) */
        if (FD_ISSET(client_rfd, &rfds)) {
            int channel, flags;
            char *payload;
            size_t payload_len;

            if (json_frame_read(client_rfd, &channel, &flags,
                                &payload, &payload_len) < 0) {
                done = 1;  /* Client disconnected */
                break;
            }

            if (channel == CHAN_PTY) {
                char type_buf[64];

                if (json_get_string(payload, "type", type_buf, sizeof(type_buf))) {
                    if (strcmp(type_buf, "input") == 0) {
                        /* Decode base64 input and write to PTY */
                        char data_buf[8192];
                        if (json_get_string(payload, "data", data_buf, sizeof(data_buf))) {
                            size_t decoded_len;
                            char *decoded = protocol_base64_decode(data_buf, &decoded_len);
                            if (decoded && decoded_len > 0) {
                                write(pty->master_fd, decoded, decoded_len);
                            }
                            free(decoded);
                        }

                    } else if (strcmp(type_buf, "resize") == 0) {
                        int new_rows = 24, new_cols = 80;
                        json_get_int(payload, "rows", &new_rows);
                        json_get_int(payload, "cols", &new_cols);
                        if (pty_resize(pty, new_rows, new_cols) == 0) {
                            json_frame_write_fmt(client_wfd, CHAN_PTY,
                                "{\"type\":\"resize_ok\",\"rows\":%d,\"cols\":%d}",
                                new_rows, new_cols);
                        } else {
                            json_frame_write_fmt(client_wfd, CHAN_PTY,
                                "{\"type\":\"error\",\"message\":\"resize failed\"}");
                        }

                    } else if (strcmp(type_buf, "signal") == 0) {
                        char sig_buf[32];
                        int signum = 0;

                        if (json_get_string(payload, "signal", sig_buf, sizeof(sig_buf))) {
                            signum = pty_parse_signal(sig_buf);
                            if (signum > 0 && pty_signal(pty, signum) == 0) {
                                json_frame_write_fmt(client_wfd, CHAN_PTY,
                                    "{\"type\":\"signal_ok\",\"signal\":\"%s\"}",
                                    sig_buf);
                            } else {
                                json_frame_write_fmt(client_wfd, CHAN_PTY,
                                    "{\"type\":\"error\",\"message\":\"signal failed\"}");
                            }
                        }

                    } else if (strcmp(type_buf, "close") == 0) {
                        done = 1;
                    }
                }
            } else if (channel == CHAN_CONTROL) {
                /* Handle control messages even in PTY mode */
                char type_buf[64];
                if (json_get_string(payload, "type", type_buf, sizeof(type_buf))) {
                    if (strcmp(type_buf, "disconnect") == 0) {
                        json_frame_write_fmt(client_wfd, CHAN_CONTROL,
                            "{\"type\":\"disconnect_ok\"}");
                        done = 1;
                    } else if (strcmp(type_buf, "ping") == 0) {
                        json_frame_write_fmt(client_wfd, CHAN_CONTROL,
                            "{\"type\":\"pong\"}");
                    }
                }
            }
            /* Ignore other channels in PTY mode */

            free(payload);
        }

        /* Check if child has exited */
        if (pty->child_pid > 0) {
            int status;
            pid_t result = waitpid(pty->child_pid, &status, WNOHANG);
            if (result == pty->child_pid) {
                pty->active = 0;
                if (WIFEXITED(status))
                    exit_code = WEXITSTATUS(status);
                else if (WIFSIGNALED(status))
                    exit_code = 128 + WTERMSIG(status);
                else
                    exit_code = -1;
                pty->child_pid = -1;

                /* Drain remaining PTY output */
                if (pty->master_fd >= 0) {
                    ssize_t n;
                    while ((n = read(pty->master_fd, pty_buf, sizeof(pty_buf))) > 0) {
                        if (pty->strip.active) {
                            n = ansi_strip(&pty->strip, pty_buf, n,
                                           pty_buf, sizeof(pty_buf));
                            if (n == 0)
                                continue;
                        }
                        char *b64 = protocol_base64_encode(pty_buf, n);
                        if (b64) {
                            json_frame_write_fmt(client_wfd, CHAN_PTY,
                                "{\"type\":\"output\",\"data\":\"%s\",\"encoding\":\"base64\"}",
                                b64);
                            free(b64);
                        }
                    }
                    close(pty->master_fd);
                    pty->master_fd = -1;
                }

                done = 1;
            }
        }
    }

    /* If we still have an active PTY, close it */
    if (pty->active || pty->master_fd >= 0) {
        int code = pty_close(pty);
        if (exit_code < 0)
            exit_code = code;
    }

    /* If we still don't have an exit code, wait for the child directly */
    if (exit_code < 0 && pty->child_pid > 0) {
        int status, i;
        for (i = 0; i < 50; i++) {  /* 5 seconds */
            if (waitpid(pty->child_pid, &status, WNOHANG) == pty->child_pid) {
                if (WIFEXITED(status))
                    exit_code = WEXITSTATUS(status);
                else if (WIFSIGNALED(status))
                    exit_code = 128 + WTERMSIG(status);
                pty->child_pid = -1;
                break;
            }
            usleep(100000);
        }
        if (pty->child_pid > 0) {
            kill(pty->child_pid, SIGKILL);
            if (waitpid(pty->child_pid, &status, 0) > 0) {
                if (WIFEXITED(status))
                    exit_code = WEXITSTATUS(status);
                else if (WIFSIGNALED(status))
                    exit_code = 128 + WTERMSIG(status);
            }
            pty->child_pid = -1;
        }
    }

    /* Close master fd if still open */
    if (pty->master_fd >= 0) {
        close(pty->master_fd);
        pty->master_fd = -1;
    }

    return exit_code;
}

/* Parse a signal name string to signal number.
 * Accepts names like "SIGINT", "SIGTERM", "INT", "TERM", etc.
 * Also accepts numeric strings like "2", "9", "15". */
int
pty_parse_signal(const char *name)
{
    /* Skip "SIG" prefix if present */
    if (strncmp(name, "SIG", 3) == 0)
        name += 3;

    /* Check numeric */
    if (name[0] >= '0' && name[0] <= '9')
        return atoi(name);

    /* Named signals */
    if (strcmp(name, "HUP") == 0)    return SIGHUP;
    if (strcmp(name, "INT") == 0)    return SIGINT;
    if (strcmp(name, "QUIT") == 0)   return SIGQUIT;
    if (strcmp(name, "KILL") == 0)   return SIGKILL;
    if (strcmp(name, "TERM") == 0)   return SIGTERM;
    if (strcmp(name, "STOP") == 0)   return SIGSTOP;
    if (strcmp(name, "TSTP") == 0)   return SIGTSTP;
    if (strcmp(name, "CONT") == 0)   return SIGCONT;
    if (strcmp(name, "WINCH") == 0)  return SIGWINCH;
    if (strcmp(name, "USR1") == 0)   return SIGUSR1;
    if (strcmp(name, "USR2") == 0)   return SIGUSR2;
    if (strcmp(name, "PIPE") == 0)   return SIGPIPE;
    if (strcmp(name, "ALRM") == 0)   return SIGALRM;
    if (strcmp(name, "CHLD") == 0)   return SIGCHLD;

    return -1;  /* Unknown */
}

/* Handle a PTY spawn request from CHAN_PTY.
 * Called from json_session_handle when a "spawn" message arrives.
 * Takes over the session loop for the duration of the PTY session.
 *
 * Returns the child exit code (sent as exit frame to client). */
int
pty_handle_spawn(int client_rfd, int client_wfd, const char *payload)
{
    pty_session_t pty;
    int rows = 24, cols = 80;
    char shell_buf[256];
    const char *shell = "/bin/bash";
    int exit_code;

    memset(&pty, 0, sizeof(pty));
    pty.master_fd = -1;
    pty.child_pid = -1;

    /* Parse spawn parameters */
    json_get_int(payload, "rows", &rows);
    json_get_int(payload, "cols", &cols);
    if (json_get_string(payload, "shell", shell_buf, sizeof(shell_buf)))
        shell = shell_buf;

    /* Check for ANSI stripping option */
    {
        char strip_buf[16];
        if (json_get_string(payload, "strip_ansi", strip_buf, sizeof(strip_buf))) {
            if (strcmp(strip_buf, "true") == 0)
                pty.strip.active = 1;
        } else {
            /* Also check as boolean (json_get_int returns 1 for true) */
            int strip_val = 0;
            if (json_get_int(payload, "strip_ansi", &strip_val) == 0 && strip_val)
                pty.strip.active = 1;
        }
    }

    /* Spawn PTY */
    if (pty_spawn(&pty, rows, cols, shell) < 0) {
        json_frame_write_fmt(client_wfd, CHAN_PTY,
            "{\"type\":\"error\",\"message\":\"pty spawn failed\"}");
        return -1;
    }

    /* Send spawn_ok */
    if (pty.strip.active) {
        json_frame_write_fmt(client_wfd, CHAN_PTY,
            "{\"type\":\"spawn_ok\",\"rows\":%d,\"cols\":%d,\"pid\":%d,\"strip_ansi\":true}",
            pty.rows, pty.cols, (int)pty.child_pid);
    } else {
        json_frame_write_fmt(client_wfd, CHAN_PTY,
            "{\"type\":\"spawn_ok\",\"rows\":%d,\"cols\":%d,\"pid\":%d}",
            pty.rows, pty.cols, (int)pty.child_pid);
    }

    /* Enter relay loop */
    exit_code = pty_relay_loop(&pty, client_rfd, client_wfd);

    /* Send exit event */
    json_frame_write_fmt(client_wfd, CHAN_PTY,
        "{\"type\":\"exit\",\"exit_code\":%d}", exit_code);

    return exit_code;
}
