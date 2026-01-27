/* server_main.c -- Main entry point for bash-server */

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
#include <getopt.h>
#include <fcntl.h>

/* Global state */
static volatile sig_atomic_t server_running = 1;
static volatile sig_atomic_t got_sigchld = 0;
static int server_fd = -1;
static server_config_t config;

/* Forward declarations */
static void print_usage(const char *progname);
static void print_version(void);
static int parse_arguments(int argc, char **argv, server_config_t *cfg);
static void reap_children(void);
static void write_pid_file(const char *path);

/* Signal handler */
void
server_signal_handler(int sig)
{
    switch (sig) {
        case SIGINT:
        case SIGTERM:
            server_running = 0;
            break;
        case SIGCHLD:
            got_sigchld = 1;
            break;
        case SIGPIPE:
            /* Ignore */
            break;
    }
}

/* Set up signal handlers */
static void
setup_signals(void)
{
    struct sigaction sa;
    
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = server_signal_handler;
    
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);
    
    /* Ignore SIGPIPE - handle errors in write() */
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
}

/* Server shutdown cleanup */
void
server_shutdown(void)
{
    if (server_fd >= 0) {
        server_socket_close(server_fd, config.socket_path);
        server_fd = -1;
    }
    
    if (config.pid_file) {
        unlink(config.pid_file);
    }
    
    /* Clean up children */
    reap_children();
}

/* Daemonize the process */
int
server_daemonize(void)
{
    pid_t pid;
    int fd;
    
    pid = fork();
    if (pid < 0)
        return -1;
    if (pid > 0)
        _exit(0);  /* Parent exits */
    
    /* Child becomes session leader */
    if (setsid() < 0)
        return -1;
    
    /* Fork again to prevent acquiring a controlling terminal */
    pid = fork();
    if (pid < 0)
        return -1;
    if (pid > 0)
        _exit(0);
    
    /* Change to root directory */
    chdir("/");
    
    /* Close standard file descriptors */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    /* Redirect to /dev/null */
    fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2)
            close(fd);
    }
    
    return 0;
}

/* Reap zombie child processes */
static void
reap_children(void)
{
    int status;
    pid_t pid;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (config.verbose) {
            fprintf(stderr, "bash-server: child %d exited with status %d\n",
                    (int)pid, WEXITSTATUS(status));
        }
    }
    got_sigchld = 0;
}

/* Write PID file */
static void
write_pid_file(const char *path)
{
    FILE *fp;
    
    fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "%d\n", (int)getpid());
        fclose(fp);
    } else {
        fprintf(stderr, "bash-server: cannot write pid file %s: %s\n",
                path, strerror(errno));
    }
}

/* Print usage message */
static void
print_usage(const char *progname)
{
    printf("Usage: %s [OPTIONS]\n\n", progname);
    printf("Options:\n");
    printf("  -s, --socket PATH   Unix socket path (required)\n");
    printf("  -a, --auth TOKEN    Authentication token (required)\n");
    printf("  -d, --daemon        Run as daemon\n");
    printf("  -p, --pidfile PATH  Write PID to file\n");
    printf("  -m, --max-clients N Maximum simultaneous clients (default: 10)\n");
    printf("  -v, --verbose       Verbose output\n");
    printf("  -h, --help          Show this help\n");
    printf("  -V, --version       Show version\n");
    printf("\n");
    printf("Example:\n");
    printf("  %s --socket /tmp/bash.sock --auth mysecret\n", progname);
}

/* Print version */
static void
print_version(void)
{
    printf("bash-server 1.0 (GNU Bash 5.1)\n");
    printf("Copyright (C) 2026 Free Software Foundation, Inc.\n");
    printf("License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>\n");
}

/* Parse command-line arguments */
static int
parse_arguments(int argc, char **argv, server_config_t *cfg)
{
    static struct option long_options[] = {
        {"socket",      required_argument, 0, 's'},
        {"auth",        required_argument, 0, 'a'},
        {"daemon",      no_argument,       0, 'd'},
        {"pidfile",     required_argument, 0, 'p'},
        {"max-clients", required_argument, 0, 'm'},
        {"verbose",     no_argument,       0, 'v'},
        {"help",        no_argument,       0, 'h'},
        {"version",     no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };
    
    int c, option_index;
    
    /* Defaults */
    memset(cfg, 0, sizeof(*cfg));
    cfg->max_clients = 10;
    
    while ((c = getopt_long(argc, argv, "s:a:dp:m:vhV", long_options, &option_index)) != -1) {
        switch (c) {
            case 's':
                cfg->socket_path = optarg;
                break;
            case 'a':
                cfg->auth_token = optarg;
                break;
            case 'd':
                cfg->daemon_mode = 1;
                break;
            case 'p':
                cfg->pid_file = optarg;
                break;
            case 'm':
                cfg->max_clients = atoi(optarg);
                if (cfg->max_clients < 1)
                    cfg->max_clients = 1;
                break;
            case 'v':
                cfg->verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            case 'V':
                print_version();
                exit(0);
            default:
                return -1;
        }
    }
    
    /* Validate required arguments */
    if (!cfg->socket_path) {
        fprintf(stderr, "bash-server: --socket is required\n");
        return -1;
    }
    if (!cfg->auth_token) {
        fprintf(stderr, "bash-server: --auth is required\n");
        return -1;
    }
    
    return 0;
}

/* Handle a client connection */
static void
handle_client(int client_fd, server_config_t *cfg)
{
    client_session_t session;
    
    if (session_init(&session, client_fd) < 0) {
        close(client_fd);
        return;
    }
    
    session_handle(&session, cfg);
    session_cleanup(&session);
}

/* Main entry point */
int
main(int argc, char **argv)
{
    int client_fd;
    pid_t pid;
    
    /* Parse arguments */
    if (parse_arguments(argc, argv, &config) < 0) {
        print_usage(argv[0]);
        exit(1);
    }
    
    /* Daemonize if requested */
    if (config.daemon_mode) {
        if (server_daemonize() < 0) {
            fprintf(stderr, "bash-server: failed to daemonize: %s\n", strerror(errno));
            exit(1);
        }
    }
    
    /* Set up signal handlers */
    setup_signals();
    
    /* Create server socket */
    server_fd = server_socket_create(config.socket_path);
    if (server_fd < 0) {
        fprintf(stderr, "bash-server: failed to create socket %s: %s\n",
                config.socket_path, strerror(errno));
        exit(1);
    }
    
    /* Write PID file */
    if (config.pid_file) {
        write_pid_file(config.pid_file);
    }
    
    if (config.verbose) {
        fprintf(stderr, "bash-server: listening on %s\n", config.socket_path);
    }
    
    /* Main accept loop */
    while (server_running) {
        /* Reap any zombie children */
        if (got_sigchld) {
            reap_children();
        }
        
        /* Accept new connection */
        client_fd = server_accept_client(server_fd);
        if (client_fd < 0) {
            if (errno == EINTR)
                continue;
            if (!server_running)
                break;
            if (config.verbose) {
                fprintf(stderr, "bash-server: accept failed: %s\n", strerror(errno));
            }
            continue;
        }
        
        if (config.verbose) {
            fprintf(stderr, "bash-server: client connected (fd=%d)\n", client_fd);
        }
        
        /* Fork to handle client */
        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "bash-server: fork failed: %s\n", strerror(errno));
            close(client_fd);
            continue;
        }
        
        if (pid == 0) {
            /* Child process */
            close(server_fd);
            handle_client(client_fd, &config);
            exit(0);
        }
        
        /* Parent process */
        close(client_fd);
    }
    
    /* Cleanup */
    server_shutdown();
    
    if (config.verbose) {
        fprintf(stderr, "bash-server: shutdown complete\n");
    }
    
    return 0;
}
