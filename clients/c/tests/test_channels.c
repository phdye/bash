/*
 * test_channels.c - Tests using socketpair mock.
 */

#include "bashclient.h"
#include "../src/internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>

static int tests_run = 0, tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %s ... ", #name); \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else printf("FAIL\n"); \
} while(0)

#define ASSERT(cond) do { if (!(cond)) { printf("[line %d: %s] ", __LINE__, #cond); return 0; } } while(0)

/* Create a client connected to a socketpair; return the server fd. */
static bc_client_t *make_mock(int *server_fd)
{
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) return NULL;

    bc_client_t *c = bc_connect_fd(fds[0]);
    *server_fd = fds[1];
    return c;
}

static void mock_send(int fd, const char *json)
{
    write(fd, json, strlen(json));
    write(fd, "\n", 1);
}

static int test_auth_ok(void)
{
    int sfd;
    bc_client_t *c = make_mock(&sfd);
    ASSERT(c != NULL);

    /* Simulate server: send auth_ok after client sends auth */
    pid_t pid = fork();
    if (pid == 0) {
        char buf[512];
        read(sfd, buf, sizeof(buf));
        mock_send(sfd, "{\"ch\":0,\"type\":\"auth_ok\",\"capabilities\":[]}");
        close(sfd);
        _exit(0);
    }

    int rc = bc_auth(c, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    ASSERT(rc == BC_OK);

    bc_close(c);
    close(sfd);
    int status;
    waitpid(pid, &status, 0);
    return 1;
}

static int test_auth_fail(void)
{
    int sfd;
    bc_client_t *c = make_mock(&sfd);
    ASSERT(c != NULL);

    pid_t pid = fork();
    if (pid == 0) {
        char buf[512];
        read(sfd, buf, sizeof(buf));
        mock_send(sfd, "{\"ch\":0,\"type\":\"error\",\"message\":\"invalid token\"}");
        close(sfd);
        _exit(0);
    }

    int rc = bc_auth(c, "bad");
    ASSERT(rc == BC_ERR_AUTH);

    bc_close(c);
    close(sfd);
    int status;
    waitpid(pid, &status, 0);
    return 1;
}

static int test_eval(void)
{
    int sfd;
    bc_client_t *c = make_mock(&sfd);
    ASSERT(c != NULL);

    pid_t pid = fork();
    if (pid == 0) {
        char buf[1024];
        read(sfd, buf, sizeof(buf)); /* read eval request */
        mock_send(sfd, "{\"ch\":1,\"type\":\"stdout\",\"data\":\"aGVsbG8K\",\"encoding\":\"base64\"}");
        mock_send(sfd, "{\"ch\":1,\"type\":\"stderr\",\"data\":\"\",\"encoding\":\"base64\"}");
        mock_send(sfd, "{\"ch\":1,\"type\":\"complete\",\"exit_code\":0}");
        close(sfd);
        _exit(0);
    }

    bc_eval_result_t result;
    int rc = bc_eval(c, "echo hello", &result);
    ASSERT(rc == BC_OK);
    ASSERT(result.stdout_data != NULL);
    ASSERT(strcmp(result.stdout_data, "hello\n") == 0);
    ASSERT(result.exit_code == 0);
    bc_eval_result_free(&result);

    bc_close(c);
    close(sfd);
    int status;
    waitpid(pid, &status, 0);
    return 1;
}

static int test_ping(void)
{
    int sfd;
    bc_client_t *c = make_mock(&sfd);
    ASSERT(c != NULL);

    pid_t pid = fork();
    if (pid == 0) {
        char buf[256];
        read(sfd, buf, sizeof(buf));
        mock_send(sfd, "{\"ch\":0,\"type\":\"pong\"}");
        close(sfd);
        _exit(0);
    }

    int rc = bc_ping(c);
    ASSERT(rc == BC_OK);

    bc_close(c);
    close(sfd);
    int status;
    waitpid(pid, &status, 0);
    return 1;
}

static int test_state_get_var(void)
{
    int sfd;
    bc_client_t *c = make_mock(&sfd);
    ASSERT(c != NULL);

    pid_t pid = fork();
    if (pid == 0) {
        char buf[512];
        read(sfd, buf, sizeof(buf));
        mock_send(sfd, "{\"ch\":2,\"type\":\"value\",\"target\":\"var\",\"name\":\"X\",\"value\":\"42\"}");
        close(sfd);
        _exit(0);
    }

    bc_var_info_t info;
    int rc = bc_state_get_var(c, "X", &info);
    ASSERT(rc == BC_OK);
    ASSERT(strcmp(info.value, "42") == 0);
    bc_var_info_free(&info);

    bc_close(c);
    close(sfd);
    int status;
    waitpid(pid, &status, 0);
    return 1;
}

int main(void)
{
    alarm(30);
    signal(SIGPIPE, SIG_IGN);
    printf("test_channels:\n");

    TEST(auth_ok);
    TEST(auth_fail);
    TEST(eval);
    TEST(ping);
    TEST(state_get_var);

    printf("%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
