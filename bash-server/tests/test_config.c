/* test_config.c -- Unit tests for server_main.c static functions
 *
 * Strategy: #include server_main.c to access static functions directly.
 * We stub out main() and the bash externs since we don't link against bash.
 *
 * Compile (from bash-server/tests/):
 *   gcc -I.. -I../.. -DHAVE_CONFIG_H -o test_config \
 *       test_config.c ../server_protocol.c ../server_socket.c
 */

#include "server.h"
#include <assert.h>
#include <sys/stat.h>
#include <limits.h>
#include <signal.h>

/* Stub out bash externs that server_session.c needs but we don't link */
int shell_initialized = 0;
int last_command_exit_value = 0;

/* Stub parse_and_execute */
int parse_and_execute(char *string, const char *from_file, int flags)
{
    (void)string; (void)from_file; (void)flags;
    return 0;
}

/* Stub shell_initialize */
void shell_initialize(void) {}

/* Stub get_string_value */
char *get_string_value(const char *name) { (void)name; return NULL; }

/* Stub session functions (called by handle_client in server_main.c) */
int session_init(client_session_t *s, int fd) { (void)s; (void)fd; return 0; }
void session_cleanup(client_session_t *s) { (void)s; }
int session_handle(client_session_t *s, server_config_t *c) { (void)s; (void)c; return 0; }

/* Rename main() in server_main.c so it doesn't conflict */
#define main server_main_original
#include "../server_main.c"
#undef main

/* Test counters */
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
 * ensure_directory tests
 * ================================================================ */
static void
test_ensure_dir_create(void)
{
    char path[PATH_MAX];
    struct stat st;
    TEST("ensure_directory: creates new directory");
    snprintf(path, sizeof(path), "/tmp/test-bashsrv-%d-create", (int)getpid());
    rmdir(path);
    if (ensure_directory(path, 0700) == 0 &&
        stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        PASS();
    else
        FAIL(strerror(errno));
    rmdir(path);
}

static void
test_ensure_dir_exists(void)
{
    char path[PATH_MAX];
    TEST("ensure_directory: existing directory returns 0");
    snprintf(path, sizeof(path), "/tmp/test-bashsrv-%d-exists", (int)getpid());
    mkdir(path, 0700);
    if (ensure_directory(path, 0700) == 0)
        PASS();
    else
        FAIL(strerror(errno));
    rmdir(path);
}

static void
test_ensure_dir_file_conflict(void)
{
    char path[PATH_MAX];
    int fd;
    TEST("ensure_directory: file at path returns ENOTDIR");
    snprintf(path, sizeof(path), "/tmp/test-bashsrv-%d-conflict", (int)getpid());
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) close(fd);
    if (ensure_directory(path, 0700) < 0 && errno == ENOTDIR)
        PASS();
    else
        FAIL("expected ENOTDIR");
    unlink(path);
}

static void
test_ensure_dir_permissions(void)
{
    char path[PATH_MAX];
    struct stat st;
    TEST("ensure_directory: directory created with mode 0700");
    snprintf(path, sizeof(path), "/tmp/test-bashsrv-%d-perm", (int)getpid());
    rmdir(path);
    ensure_directory(path, 0700);
    if (stat(path, &st) == 0 && (st.st_mode & 0777) == 0700)
        PASS();
    else
        FAIL("wrong permissions");
    rmdir(path);
}

/* ================================================================
 * read_config_file tests
 * ================================================================ */
static void
test_config_file_socket(void)
{
    server_config_t cfg;
    char dir[PATH_MAX], path[PATH_MAX], old_home[PATH_MAX];
    const char *home;
    FILE *fp;

    TEST("read_config_file: reads socket directive");

    /* Create a temp dir to act as HOME */
    snprintf(dir, sizeof(dir), "/tmp/test-bashsrv-%d-cfg", (int)getpid());
    mkdir(dir, 0700);
    snprintf(path, sizeof(path), "%s/.bash-serverrc", dir);

    fp = fopen(path, "w");
    if (!fp) { FAIL("cannot create config file"); return; }
    fprintf(fp, "# Comment line\n");
    fprintf(fp, "\n");
    fprintf(fp, "socket /custom/path/sock\n");
    fclose(fp);

    /* Temporarily override HOME */
    home = getenv("HOME");
    if (home) strncpy(old_home, home, sizeof(old_home) - 1);
    setenv("HOME", dir, 1);

    memset(&cfg, 0, sizeof(cfg));
    read_config_file(&cfg);

    if (cfg.socket_path && strcmp(cfg.socket_path, "/custom/path/sock") == 0)
        PASS();
    else
        FAIL(cfg.socket_path ? cfg.socket_path : "NULL");

    free(cfg.socket_path);
    unlink(path);
    rmdir(dir);
    if (home) setenv("HOME", old_home, 1);
}

static void
test_config_file_skip_comment(void)
{
    server_config_t cfg;
    char dir[PATH_MAX], path[PATH_MAX], old_home[PATH_MAX];
    const char *home;
    FILE *fp;

    TEST("read_config_file: skips comments and blank lines");

    snprintf(dir, sizeof(dir), "/tmp/test-bashsrv-%d-cmt", (int)getpid());
    mkdir(dir, 0700);
    snprintf(path, sizeof(path), "%s/.bash-serverrc", dir);

    fp = fopen(path, "w");
    if (!fp) { FAIL("cannot create config file"); return; }
    fprintf(fp, "# This is a comment\n");
    fprintf(fp, "   # Indented comment\n");
    fprintf(fp, "\n");
    fprintf(fp, "   \n");
    fprintf(fp, "\t\n");
    fclose(fp);

    home = getenv("HOME");
    if (home) strncpy(old_home, home, sizeof(old_home) - 1);
    setenv("HOME", dir, 1);

    memset(&cfg, 0, sizeof(cfg));
    read_config_file(&cfg);

    if (!cfg.socket_path)
        PASS();
    else
        FAIL("expected NULL socket_path");

    unlink(path);
    rmdir(dir);
    if (home) setenv("HOME", old_home, 1);
}

static void
test_config_file_no_override(void)
{
    server_config_t cfg;
    char dir[PATH_MAX], path[PATH_MAX], old_home[PATH_MAX];
    const char *home;
    FILE *fp;

    TEST("read_config_file: CLI-set path not overridden");

    snprintf(dir, sizeof(dir), "/tmp/test-bashsrv-%d-noo", (int)getpid());
    mkdir(dir, 0700);
    snprintf(path, sizeof(path), "%s/.bash-serverrc", dir);

    fp = fopen(path, "w");
    if (!fp) { FAIL("cannot create config file"); return; }
    fprintf(fp, "socket /from-config\n");
    fclose(fp);

    home = getenv("HOME");
    if (home) strncpy(old_home, home, sizeof(old_home) - 1);
    setenv("HOME", dir, 1);

    memset(&cfg, 0, sizeof(cfg));
    cfg.socket_path = strdup("/from-cli");
    read_config_file(&cfg);

    if (cfg.socket_path && strcmp(cfg.socket_path, "/from-cli") == 0)
        PASS();
    else
        FAIL(cfg.socket_path ? cfg.socket_path : "NULL");

    free(cfg.socket_path);
    unlink(path);
    rmdir(dir);
    if (home) setenv("HOME", old_home, 1);
}

static void
test_config_file_missing(void)
{
    server_config_t cfg;
    char dir[PATH_MAX], old_home[PATH_MAX];
    const char *home;

    TEST("read_config_file: missing file is not an error");

    snprintf(dir, sizeof(dir), "/tmp/test-bashsrv-%d-miss", (int)getpid());
    mkdir(dir, 0700);

    home = getenv("HOME");
    if (home) strncpy(old_home, home, sizeof(old_home) - 1);
    setenv("HOME", dir, 1);

    memset(&cfg, 0, sizeof(cfg));
    int rc = read_config_file(&cfg);

    if (rc == 0 && !cfg.socket_path)
        PASS();
    else
        FAIL("expected rc=0, socket_path=NULL");

    rmdir(dir);
    if (home) setenv("HOME", old_home, 1);
}

static void
test_config_file_whitespace_handling(void)
{
    server_config_t cfg;
    char dir[PATH_MAX], path[PATH_MAX], old_home[PATH_MAX];
    const char *home;
    FILE *fp;

    TEST("read_config_file: handles leading/trailing whitespace");

    snprintf(dir, sizeof(dir), "/tmp/test-bashsrv-%d-ws", (int)getpid());
    mkdir(dir, 0700);
    snprintf(path, sizeof(path), "%s/.bash-serverrc", dir);

    fp = fopen(path, "w");
    if (!fp) { FAIL("cannot create config file"); return; }
    fprintf(fp, "  \tsocket   /spaced/path  \t \n");
    fclose(fp);

    home = getenv("HOME");
    if (home) strncpy(old_home, home, sizeof(old_home) - 1);
    setenv("HOME", dir, 1);

    memset(&cfg, 0, sizeof(cfg));
    read_config_file(&cfg);

    if (cfg.socket_path && strcmp(cfg.socket_path, "/spaced/path") == 0)
        PASS();
    else
        FAIL(cfg.socket_path ? cfg.socket_path : "NULL");

    free(cfg.socket_path);
    unlink(path);
    rmdir(dir);
    if (home) setenv("HOME", old_home, 1);
}

/* ================================================================
 * resolve_socket_path tests
 * ================================================================ */
static void
test_resolve_cli_takes_precedence(void)
{
    server_config_t cfg;
    TEST("resolve_socket_path: CLI path takes precedence");
    memset(&cfg, 0, sizeof(cfg));
    cfg.socket_path = strdup("/cli/path");
    /* Set env to something else to prove CLI wins */
    setenv("BASH_SERVER_SOCKET", "/env/path", 1);
    int rc = resolve_socket_path(&cfg);
    if (rc == 0 && strcmp(cfg.socket_path, "/cli/path") == 0)
        PASS();
    else
        FAIL(cfg.socket_path ? cfg.socket_path : "NULL");
    free(cfg.socket_path);
    unsetenv("BASH_SERVER_SOCKET");
}

static void
test_resolve_env_second(void)
{
    server_config_t cfg;
    TEST("resolve_socket_path: env var used when no CLI");
    memset(&cfg, 0, sizeof(cfg));
    setenv("BASH_SERVER_SOCKET", "/env/sock", 1);
    int rc = resolve_socket_path(&cfg);
    if (rc == 0 && cfg.socket_path && strcmp(cfg.socket_path, "/env/sock") == 0)
        PASS();
    else
        FAIL(cfg.socket_path ? cfg.socket_path : "NULL");
    free(cfg.socket_path);
    unsetenv("BASH_SERVER_SOCKET");
}

static void
test_resolve_default_fallback(void)
{
    server_config_t cfg;
    char expected[PATH_MAX];
    char dir_to_clean[PATH_MAX];
    TEST("resolve_socket_path: falls back to /tmp default");
    memset(&cfg, 0, sizeof(cfg));
    unsetenv("BASH_SERVER_SOCKET");
    unsetenv("XDG_RUNTIME_DIR");
    /* Point HOME to a dir with no config file */
    char tmpdir[PATH_MAX];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/test-bashsrv-%d-def", (int)getpid());
    mkdir(tmpdir, 0700);
    setenv("HOME", tmpdir, 1);

    snprintf(expected, sizeof(expected), "/tmp/bash-server-%d/sock", (int)getuid());
    snprintf(dir_to_clean, sizeof(dir_to_clean), "/tmp/bash-server-%d", (int)getuid());

    int rc = resolve_socket_path(&cfg);
    if (rc == 0 && cfg.socket_path && strcmp(cfg.socket_path, expected) == 0)
        PASS();
    else
        FAIL(cfg.socket_path ? cfg.socket_path : "NULL");
    free(cfg.socket_path);
    rmdir(dir_to_clean);
    rmdir(tmpdir);
}

static void
test_resolve_xdg_runtime(void)
{
    server_config_t cfg;
    char xdg_dir[PATH_MAX], expected[PATH_MAX], bs_dir[PATH_MAX];
    TEST("resolve_socket_path: XDG_RUNTIME_DIR used");
    memset(&cfg, 0, sizeof(cfg));
    unsetenv("BASH_SERVER_SOCKET");
    /* Point HOME to a dir with no config file */
    char tmpdir[PATH_MAX];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/test-bashsrv-%d-xdg", (int)getpid());
    mkdir(tmpdir, 0700);
    setenv("HOME", tmpdir, 1);

    snprintf(xdg_dir, sizeof(xdg_dir), "/tmp/test-bashsrv-%d-xdgrt", (int)getpid());
    mkdir(xdg_dir, 0700);
    setenv("XDG_RUNTIME_DIR", xdg_dir, 1);

    snprintf(bs_dir, sizeof(bs_dir), "%s/bash-server", xdg_dir);
    snprintf(expected, sizeof(expected), "%s/bash-server/sock", xdg_dir);

    int rc = resolve_socket_path(&cfg);
    if (rc == 0 && cfg.socket_path && strcmp(cfg.socket_path, expected) == 0)
        PASS();
    else
        FAIL(cfg.socket_path ? cfg.socket_path : "NULL");
    free(cfg.socket_path);
    rmdir(bs_dir);
    rmdir(xdg_dir);
    rmdir(tmpdir);
    unsetenv("XDG_RUNTIME_DIR");
}

/* ================================================================
 * generate_auth_token tests
 * ================================================================ */
static void
test_auth_token_generated(void)
{
    server_config_t cfg;
    char dir[PATH_MAX], sock_path[PATH_MAX];
    TEST("generate_auth_token: produces 64-char hex token");
    snprintf(dir, sizeof(dir), "/tmp/test-bashsrv-%d-tok", (int)getpid());
    mkdir(dir, 0700);
    snprintf(sock_path, sizeof(sock_path), "%s/sock", dir);

    memset(&cfg, 0, sizeof(cfg));
    cfg.socket_path = sock_path;
    int rc = generate_auth_token(&cfg);
    if (rc == 0 && cfg.auth_token && strlen(cfg.auth_token) == 64) {
        /* Verify all hex chars */
        int ok = 1;
        for (int i = 0; i < 64; i++) {
            char c = cfg.auth_token[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                ok = 0;
                break;
            }
        }
        if (ok) PASS(); else FAIL("non-hex chars in token");
    } else {
        FAIL(cfg.auth_token ? "wrong length" : "NULL token");
    }
    if (cfg.auth_file) unlink(cfg.auth_file);
    free(cfg.auth_token);
    free(cfg.auth_file);
    rmdir(dir);
}

static void
test_auth_token_file_created(void)
{
    server_config_t cfg;
    char dir[PATH_MAX], sock_path[PATH_MAX], expected_file[PATH_MAX];
    struct stat st;
    TEST("generate_auth_token: creates .token file with 0600");
    snprintf(dir, sizeof(dir), "/tmp/test-bashsrv-%d-tokf", (int)getpid());
    mkdir(dir, 0700);
    snprintf(sock_path, sizeof(sock_path), "%s/sock", dir);
    snprintf(expected_file, sizeof(expected_file), "%s/sock.token", dir);

    memset(&cfg, 0, sizeof(cfg));
    cfg.socket_path = sock_path;
    int rc = generate_auth_token(&cfg);
    if (rc == 0 && stat(expected_file, &st) == 0 && (st.st_mode & 0777) == 0600)
        PASS();
    else
        FAIL("token file missing or wrong permissions");
    if (cfg.auth_file) unlink(cfg.auth_file);
    free(cfg.auth_token);
    free(cfg.auth_file);
    rmdir(dir);
}

static void
test_auth_token_file_contents(void)
{
    server_config_t cfg;
    char dir[PATH_MAX], sock_path[PATH_MAX];
    char filebuf[128];
    int fd;
    ssize_t n;
    TEST("generate_auth_token: file contains token + newline");
    snprintf(dir, sizeof(dir), "/tmp/test-bashsrv-%d-tokc", (int)getpid());
    mkdir(dir, 0700);
    snprintf(sock_path, sizeof(sock_path), "%s/sock", dir);

    memset(&cfg, 0, sizeof(cfg));
    cfg.socket_path = sock_path;
    int rc = generate_auth_token(&cfg);
    if (rc != 0) { FAIL("generate failed"); goto clean; }

    fd = open(cfg.auth_file, O_RDONLY);
    if (fd < 0) { FAIL("cannot open token file"); goto clean; }
    n = read(fd, filebuf, sizeof(filebuf));
    close(fd);
    filebuf[n] = '\0';

    /* Should be 64 hex chars + newline = 65 bytes */
    if (n == 65 && filebuf[64] == '\n' && strncmp(filebuf, cfg.auth_token, 64) == 0)
        PASS();
    else
        FAIL("wrong file contents");

clean:
    if (cfg.auth_file) unlink(cfg.auth_file);
    free(cfg.auth_token);
    free(cfg.auth_file);
    rmdir(dir);
}

static void
test_auth_token_uniqueness(void)
{
    server_config_t cfg1, cfg2;
    char dir[PATH_MAX], sp1[PATH_MAX], sp2[PATH_MAX];
    TEST("generate_auth_token: two calls produce different tokens");
    snprintf(dir, sizeof(dir), "/tmp/test-bashsrv-%d-uniq", (int)getpid());
    mkdir(dir, 0700);
    snprintf(sp1, sizeof(sp1), "%s/s1", dir);
    snprintf(sp2, sizeof(sp2), "%s/s2", dir);

    memset(&cfg1, 0, sizeof(cfg1)); cfg1.socket_path = sp1;
    memset(&cfg2, 0, sizeof(cfg2)); cfg2.socket_path = sp2;

    int rc1 = generate_auth_token(&cfg1);
    int rc2 = generate_auth_token(&cfg2);

    if (rc1 == 0 && rc2 == 0 && strcmp(cfg1.auth_token, cfg2.auth_token) != 0)
        PASS();
    else
        FAIL("tokens are identical");

    if (cfg1.auth_file) unlink(cfg1.auth_file);
    if (cfg2.auth_file) unlink(cfg2.auth_file);
    free(cfg1.auth_token); free(cfg1.auth_file);
    free(cfg2.auth_token); free(cfg2.auth_file);
    rmdir(dir);
}

/* ================================================================
 * parse_arguments tests
 * ================================================================ */
static void
test_parse_args_defaults(void)
{
    server_config_t cfg;
    char *argv[] = {"bash-server", NULL};
    TEST("parse_arguments: defaults (no args)");
    optind = 1;  /* Reset getopt */
    int rc = parse_arguments(1, argv, &cfg);
    if (rc == 0 && cfg.max_clients == 10 && !cfg.daemon_mode && !cfg.verbose &&
        !cfg.socket_path && !cfg.pid_file)
        PASS();
    else
        FAIL("unexpected defaults");
}

static void
test_parse_args_socket(void)
{
    server_config_t cfg;
    char *argv[] = {"bash-server", "--socket", "/my/sock", NULL};
    TEST("parse_arguments: --socket /my/sock");
    optind = 1;
    int rc = parse_arguments(3, argv, &cfg);
    if (rc == 0 && cfg.socket_path && strcmp(cfg.socket_path, "/my/sock") == 0)
        PASS();
    else
        FAIL(cfg.socket_path ? cfg.socket_path : "NULL");
}

static void
test_parse_args_daemon_verbose(void)
{
    server_config_t cfg;
    char *argv[] = {"bash-server", "-d", "-v", NULL};
    TEST("parse_arguments: -d -v");
    optind = 1;
    int rc = parse_arguments(3, argv, &cfg);
    if (rc == 0 && cfg.daemon_mode == 1 && cfg.verbose == 1)
        PASS();
    else
        FAIL("flags not set");
}

static void
test_parse_args_max_clients(void)
{
    server_config_t cfg;
    char *argv[] = {"bash-server", "-m", "25", NULL};
    TEST("parse_arguments: -m 25");
    optind = 1;
    int rc = parse_arguments(3, argv, &cfg);
    if (rc == 0 && cfg.max_clients == 25)
        PASS();
    else
        FAIL("wrong max_clients");
}

static void
test_parse_args_max_clients_zero(void)
{
    server_config_t cfg;
    char *argv[] = {"bash-server", "-m", "0", NULL};
    TEST("parse_arguments: -m 0 (clamped to 1)");
    optind = 1;
    int rc = parse_arguments(3, argv, &cfg);
    if (rc == 0 && cfg.max_clients == 1)
        PASS();
    else
        FAIL("not clamped to 1");
}

static void
test_parse_args_negative_clients(void)
{
    server_config_t cfg;
    char *argv[] = {"bash-server", "-m", "-5", NULL};
    TEST("parse_arguments: -m -5 (clamped to 1)");
    optind = 1;
    int rc = parse_arguments(3, argv, &cfg);
    if (rc == 0 && cfg.max_clients == 1)
        PASS();
    else
        FAIL("not clamped to 1");
}

/* ================================================================
 * Main
 * ================================================================ */
int
main(void)
{
    alarm(10);

    printf("=== bash-server config/auth unit tests ===\n\n");

    printf("[ensure_directory]\n");
    test_ensure_dir_create();
    test_ensure_dir_exists();
    test_ensure_dir_file_conflict();
    test_ensure_dir_permissions();

    printf("\n[read_config_file]\n");
    test_config_file_socket();
    test_config_file_skip_comment();
    test_config_file_no_override();
    test_config_file_missing();
    test_config_file_whitespace_handling();

    printf("\n[resolve_socket_path]\n");
    test_resolve_cli_takes_precedence();
    test_resolve_env_second();
    test_resolve_default_fallback();
    test_resolve_xdg_runtime();

    printf("\n[generate_auth_token]\n");
    test_auth_token_generated();
    test_auth_token_file_created();
    test_auth_token_file_contents();
    test_auth_token_uniqueness();

    printf("\n[parse_arguments]\n");
    test_parse_args_defaults();
    test_parse_args_socket();
    test_parse_args_daemon_verbose();
    test_parse_args_max_clients();
    test_parse_args_max_clients_zero();
    test_parse_args_negative_clients();

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
