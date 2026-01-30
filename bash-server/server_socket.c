/* server_socket.c -- Unix socket handling for bash-server */

/* Copyright (C) 2026 Free Software Foundation, Inc.

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
   along with Bash.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "server.h"
#include <sys/stat.h>
#include <fcntl.h>

/* Create and bind a Unix domain socket */
int
server_socket_create(const char *path)
{
    int fd;
    struct sockaddr_un addr;
    int reuse = 1;
    
    /* Validate path length */
    if (strlen(path) >= sizeof(addr.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    
    /* Create socket */
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    
    /* Set socket options */
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

#ifdef __CYGWIN__
    /* Work around Cygwin AF_UNIX handshake mismatch: Python's socket module
       sets SO_PEERCRED to disable credential exchange, but C programs don't.
       The mismatch causes ECONNABORTED on accept().  Setting SO_PEERCRED on
       the server side makes both ends agree to skip the handshake.
       See: issue/cygwin-transport-alternatives.md */
    {
        int peercred = 1;
        setsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peercred, sizeof(peercred));
    }
#endif
    
    /* Remove existing socket file if present */
    unlink(path);
    
    /* Bind to path */
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    
    /* Set socket permissions (readable/writable by owner only) */
    chmod(path, 0600);
    
    /* Listen for connections */
    if (listen(fd, SOMAXCONN) < 0) {
        int saved_errno = errno;
        close(fd);
        unlink(path);
        errno = saved_errno;
        return -1;
    }
    
    return fd;
}

/* Close server socket and clean up */
void
server_socket_close(int fd, const char *path)
{
    if (fd >= 0) {
        close(fd);
    }
    if (path) {
        unlink(path);
    }
}

/* Accept a client connection */
int
server_accept_client(int server_fd)
{
    struct sockaddr_storage client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd;

    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0)
        return -1;

    /* Set close-on-exec flag */
    fcntl(client_fd, F_SETFD, FD_CLOEXEC);

    return client_fd;
}

/* Set socket to non-blocking mode */
int
server_socket_set_nonblocking(int fd)
{
    int flags;
    
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Set socket to blocking mode */
int
server_socket_set_blocking(int fd)
{
    int flags;
    
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}
