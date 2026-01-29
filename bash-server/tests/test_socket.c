/* test_socket.c -- Unit tests for server_socket.c
 *
 * Compile (from bash-server/tests/):
 *   gcc -I.. -I../.. -DHAVE_CONFIG_H -o test_socket \
 *       test_socket.c ../server_socket.c
 */

#include "server.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>

/* Stubs */
int shell_initialized = 0;
int last_command_exit_value = 0;
int parse_and_execute(char *s, const char *f, int fl) { (void)s;(void)f;(void)fl; return 0; }
void shell_initialize(void) {}
char *get_string_value(const char *n) { (void)n; return NULL; }

/* Extern for nonblocking helpers declared in server_socket.c but not in server.h */
extern int server_socket_set_nonblocking(int fd);
extern int server_socket_set_blocking(int fd);

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-55s ", name); \
    fflush(stdout); \
} while(0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { tests_failed++; printf("FAIL: %s\n", msg); } while(0)

/* ================================================================
 * Socket creation tests
 * ================================================================ */
static void
test_socket_create_basic(void)
{
    char path[PATH_MAX];
    int fd;
    struct stat st;
    TEST("server_socket_create: creates socket file");
    snprintf(path, sizeof(path), "/tmp/test-bashsrv-%d.sock", (int)getpid());
    unlink(path);
    fd = server_socket_create(path, 0);
    if (fd >= 0 && stat(path, &st) == 0 && S_ISSOCK(st.st_mode))
        PASS();
    else
        FAIL(strerror(errno));
    server_socket_close(fd, path);
}

static void
test_socket_permissions(void)
{
    char path[PATH_MAX];
    int fd;
    struct stat st;
    TEST("server_socket_create: socket file is mode 0600");
    snprintf(path, sizeof(path), "/tmp/test-bashsrv-%d-perm.sock", (int)getpid());
    unlink(path);
    fd = server_socket_create(path, 0);
    if (fd >= 0 && stat(path, &st) == 0 && (st.st_mode & 0777) == 0600)
        PASS();
    else
        FAIL("wrong permissions");
    server_socket_close(fd, path);
}

static void
test_socket_replaces_stale(void)
{
    char path[PATH_MAX];
    int fd1, fd2;
    TEST("server_socket_create: replaces stale socket file");
    snprintf(path, sizeof(path), "/tmp/test-bashsrv-%d-stale.sock", (int)getpid());
    /* Create first socket */
    fd1 = server_socket_create(path, 0);
    if (fd1 < 0) { FAIL("first create failed"); return; }
    close(fd1);  /* Close fd but leave file */
    /* Create second socket at same path */
    fd2 = server_socket_create(path, 0);
    if (fd2 >= 0)
        PASS();
    else
        FAIL("second create failed");
    server_socket_close(fd2, path);
}

static void
test_socket_close_cleanup(void)
{
    char path[PATH_MAX];
    int fd;
    struct stat st;
    TEST("server_socket_close: removes socket file");
    snprintf(path, sizeof(path), "/tmp/test-bashsrv-%d-close.sock", (int)getpid());
    fd = server_socket_create(path, 0);
    if (fd < 0) { FAIL("create failed"); return; }
    server_socket_close(fd, path);
    if (stat(path, &st) < 0 && errno == ENOENT)
        PASS();
    else
        FAIL("socket file still exists");
}

static void
test_socket_accept_connect(void)
{
    char path[PATH_MAX];
    int srv_fd, cli_fd, accepted_fd;
    struct sockaddr_un addr;
    pid_t child;
    int status;

    TEST("server_accept_client: accepts a connection");
    snprintf(path, sizeof(path), "/tmp/test-bashsrv-%d-acc.sock", (int)getpid());
    srv_fd = server_socket_create(path, 0);
    if (srv_fd < 0) { FAIL("create failed"); return; }

    /* Fork a child to connect */
    child = fork();
    if (child == 0) {
        cli_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
        if (connect(cli_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
            _exit(1);
        write(cli_fd, "hi\n", 3);
        close(cli_fd);
        _exit(0);
    }

    accepted_fd = server_accept_client(srv_fd);
    if (accepted_fd >= 0) {
        char buf[16];
        ssize_t n = read(accepted_fd, buf, sizeof(buf));
        if (n == 3 && memcmp(buf, "hi\n", 3) == 0)
            PASS();
        else
            FAIL("wrong data from client");
        close(accepted_fd);
    } else {
        FAIL("accept failed");
    }

    /* Reap child with timeout */
    for (int i = 0; i < 50; i++) {
        if (waitpid(child, &status, WNOHANG) == child) break;
        usleep(100000);
    }
    kill(child, SIGKILL);
    waitpid(child, NULL, 0);

    server_socket_close(srv_fd, path);
}

static void
test_socket_nonblocking(void)
{
    char path[PATH_MAX];
    int fd, flags;
    TEST("server_socket_set_nonblocking: sets O_NONBLOCK");
    snprintf(path, sizeof(path), "/tmp/test-bashsrv-%d-nb.sock", (int)getpid());
    fd = server_socket_create(path, 0);
    if (fd < 0) { FAIL("create failed"); return; }
    server_socket_set_nonblocking(fd);
    flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0 && (flags & O_NONBLOCK))
        PASS();
    else
        FAIL("O_NONBLOCK not set");
    server_socket_close(fd, path);
}

static void
test_socket_blocking_restore(void)
{
    char path[PATH_MAX];
    int fd, flags;
    TEST("server_socket_set_blocking: clears O_NONBLOCK");
    snprintf(path, sizeof(path), "/tmp/test-bashsrv-%d-bl.sock", (int)getpid());
    fd = server_socket_create(path, 0);
    if (fd < 0) { FAIL("create failed"); return; }
    server_socket_set_nonblocking(fd);
    server_socket_set_blocking(fd);
    flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0 && !(flags & O_NONBLOCK))
        PASS();
    else
        FAIL("O_NONBLOCK still set");
    server_socket_close(fd, path);
}

static void
test_socket_path_too_long(void)
{
    char path[256];
    int fd;
    TEST("server_socket_create: rejects overly long path");
    /* Fill with 200 chars - should exceed sun_path (108 on most systems) */
    memset(path, 'x', 200);
    path[0] = '/';
    path[4] = '/';
    path[200] = '\0';
    fd = server_socket_create(path, 0);
    if (fd < 0 && errno == ENAMETOOLONG)
        PASS();
    else {
        if (fd >= 0) close(fd);
        FAIL("expected ENAMETOOLONG");
    }
}

/* ================================================================
 * Main
 * ================================================================ */
int
main(void)
{
    alarm(30);  /* Integration-level timeout */

    printf("=== bash-server socket unit tests ===\n\n");

    printf("[socket creation]\n");
    test_socket_create_basic();
    test_socket_permissions();
    test_socket_replaces_stale();
    test_socket_close_cleanup();
    test_socket_path_too_long();

    printf("\n[socket accept/connect]\n");
    test_socket_accept_connect();

    printf("\n[socket nonblocking]\n");
    test_socket_nonblocking();
    test_socket_blocking_restore();

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
