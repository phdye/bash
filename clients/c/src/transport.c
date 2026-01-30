/*
 * transport.c - Unix socket, stdio, fd, Named Pipe transports
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <signal.h>

int bc_transport_open_unix(bc_transport_t *t, const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    t->type = BC_TRANSPORT_UNIX;
    t->fd_read = fd;
    t->fd_write = fd;
    t->child_pid = 0;
    t->fp_read = fdopen(fd, "r");
    if (!t->fp_read) {
        close(fd);
        return -1;
    }
    return 0;
}

int bc_transport_open_stdio(bc_transport_t *t, const char *const argv[])
{
    int to_child[2], from_child[2];
    if (pipe(to_child) < 0) return -1;
    if (pipe(from_child) < 0) {
        close(to_child[0]); close(to_child[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(to_child[0]); close(to_child[1]);
        close(from_child[0]); close(from_child[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child */
        close(to_child[1]);
        close(from_child[0]);
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        close(to_child[0]);
        close(from_child[1]);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }

    /* Parent */
    close(to_child[0]);
    close(from_child[1]);

    t->type = BC_TRANSPORT_STDIO;
    t->fd_read = from_child[0];
    t->fd_write = to_child[1];
    t->child_pid = pid;
    t->fp_read = fdopen(from_child[0], "r");
    if (!t->fp_read) {
        close(from_child[0]);
        close(to_child[1]);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return -1;
    }
    return 0;
}

int bc_transport_open_fd(bc_transport_t *t, int fd)
{
    t->type = BC_TRANSPORT_FD;
    t->fd_read = fd;
    t->fd_write = fd;
    t->child_pid = 0;
    t->fp_read = fdopen(dup(fd), "r");
    if (!t->fp_read) return -1;
    return 0;
}

int bc_transport_open_pipe(bc_transport_t *t, const char *pipe_name)
{
    /* On Cygwin, Named Pipes are accessible via filesystem paths */
    return bc_transport_open_unix(t, pipe_name);
}

int bc_transport_readline(bc_transport_t *t, char *buf, size_t bufsize)
{
    if (!t->fp_read) return -1;
    if (!fgets(buf, (int)bufsize, t->fp_read)) return -1;
    return (int)strlen(buf);
}

int bc_transport_write(bc_transport_t *t, const char *data, size_t len)
{
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(t->fd_write, data + written, len - written);
        if (n <= 0) return -1;
        written += n;
    }
    return 0;
}

void bc_transport_close(bc_transport_t *t)
{
    if (t->fp_read) {
        fclose(t->fp_read);
        t->fp_read = NULL;
    }
    if (t->type == BC_TRANSPORT_UNIX) {
        /* fd_read already closed by fclose */
        t->fd_read = -1;
        t->fd_write = -1;
    } else {
        if (t->fd_write >= 0) {
            close(t->fd_write);
            t->fd_write = -1;
        }
        t->fd_read = -1;
    }
    if (t->child_pid > 0) {
        kill(t->child_pid, SIGTERM);
        waitpid(t->child_pid, NULL, 0);
        t->child_pid = 0;
    }
}
