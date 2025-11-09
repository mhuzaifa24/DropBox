// test/test_client.c
#include "../include/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>


// Colors & symbols
#define GREEN   "\033[0;32m"
#define RED     "\033[0;31m"
#define YELLOW  "\033[1;33m"
#define BLUE    "\033[0;34m"
#define CYAN    "\033[0;36m"
#define MAGENTA "\033[0;35m"
#define RESET   "\033[0m"

#define BUF_SIZ 8192

// ------------------------- Helpers -------------------------
static void print_divider() {
    printf("%s------------------------------------------%s\n", CYAN, RESET);
}

static void log_info(const char *fmt, ...) {
    va_list ap;
    printf("%s[INFO] %s📣  ", BLUE, RESET);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static void log_action(const char *fmt, ...) {
    va_list ap;
    printf("%s[ACTION] %s⚙️  ", MAGENTA, RESET);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static void log_server_line(const char *line) {
    // Filter ACK lines to reduce clutter
    if (strncmp(line, "ACK:", 4) == 0) {
        // skip printing ACK lines (internal queue ack)
        return;
    }
    // Print other server lines
    printf("%s[SERVER]%s 📥 %s\n", YELLOW, RESET, line);
}

static void log_success(const char *fmt, ...) {
    va_list ap;
    printf("%s[SUCCESS] %s✅  ", GREEN, RESET);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static void log_error(const char *fmt, ...) {
    va_list ap;
    printf("%s[ERROR] %s❌  ", RED, RESET);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

// Trim helpers
static char *trim(char *s) {
    if (!s) return s;
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

// Receive available data with a short gathering window, return malloc'd buffer (or NULL)
static char *recv_with_gather(int sockfd, int initial_timeout_ms) {
    // Read with select. After first read, gather remaining with short timeout.
    fd_set readfds;
    struct timeval tv;
    char *acc = NULL;
    size_t acc_len = 0;

    // initial wait
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);
    tv.tv_sec = initial_timeout_ms / 1000;
    tv.tv_usec = (initial_timeout_ms % 1000) * 1000;

    int rv = select(sockfd + 1, &readfds, NULL, NULL, &tv);
    if (rv <= 0) return NULL; // no data

    // Now read while data available; after first read use short timeout to collect the rest
    for (;;) {
        char buf[BUF_SIZ];
        ssize_t n = recv(sockfd, buf, sizeof(buf), 0);
        if (n <= 0) break;

        char *newacc = realloc(acc, acc_len + (size_t)n + 1);
        if (!newacc) { free(acc); return NULL; }
        acc = newacc;
        memcpy(acc + acc_len, buf, (size_t)n);
        acc_len += (size_t)n;
        acc[acc_len] = '\0';

        // short gather window
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 150000; // 150ms gather
        rv = select(sockfd + 1, &readfds, NULL, NULL, &tv);
        if (rv <= 0) break;
    }

    return acc;
}

// Process response buffer (may include multiple lines). Prints lines (except ACK).
// Returns 1 if any line contained "TASK_COMPLETE: SUCCESS - " and returns pointer to the substring
// after that prefix via out_result (malloc'd). Caller must free out_result.
static int process_and_print_response(const char *buf, char **out_result) {
    if (!buf) return 0;
    *out_result = NULL;
    int found_task_complete = 0;
    const char *p = buf;
    char line[BUF_SIZ];

    while (*p) {
        // read up to newline
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';

        char *t = trim(line);
        if (t && *t) {
            // print server line unless ACK
            if (strncmp(t, "ACK:", 4) != 0) {
                printf("%s[SERVER]%s 📥 %s\n", YELLOW, RESET, t);
            }
            // detect TASK_COMPLETE payload
            const char *prefix = "TASK_COMPLETE: SUCCESS - ";
            if (strncmp(t, prefix, strlen(prefix)) == 0) {
                found_task_complete = 1;
                const char *payload = t + strlen(prefix);
                if (payload && *payload) {
                    // save payload (may be multi-line in original buffer; we only get single-line fragments here)
                    *out_result = strdup(payload);
                } else {
                    // maybe the file content comes in subsequent chunks of the original buffer; if so,
                    // take everything after the prefix in the original buf
                    const char *fullpos = strstr(buf, prefix);
                    if (fullpos) {
                        fullpos += strlen(prefix);
                        *out_result = strdup(fullpos);
                    }
                }
            }
        }

        if (!nl) break;
        p = nl + 1;
    }
    return found_task_complete;
}

// ------------------------- Network -------------------------
static int connect_to_server(const char *host, int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return -1; }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) { close(s); return -1; }
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(s); return -1; }
    printf("%s[CLIENT]%s 🌐 Connected to %s:%d\n", GREEN, RESET, host, port);
    return s;
}

// ------------------------- Test scenarios -------------------------
void test_scenario_1(int sockfd) {
    print_divider();
    log_info("Test Scenario 1: Basic user flow (signup, list, upload, list)");
    // Wait for auth prompt
    char *resp = recv_with_gather(sockfd, 2000);
    if (resp) { process_and_print_response(resp, &(char*){NULL}); free(resp); }

    // Signup
    log_action("Signing up user1");
    send(sockfd, "SIGNUP user1 password123\n", 25, 0);
    resp = recv_with_gather(sockfd, 1500);
    if (resp) { process_and_print_response(resp, &(char*){NULL}); free(resp); }

    // Wait for welcome (could be included)
    // LIST
    log_action("Listing files (should be empty)");
    send(sockfd, "LIST\n", 5, 0);
    resp = recv_with_gather(sockfd, 1500);
    if (resp) { process_and_print_response(resp, &(char*){NULL}); free(resp); }

    // Upload ../test.txt
    log_action("Uploading ../test.txt");
    FILE *f = fopen("../test.txt", "rb");
    if (!f) {
        log_error("Cannot open ../test.txt - create it in project root and retry");
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)sz);
    fread(buf, 1, (size_t)sz, f);
    fclose(f);

    send(sockfd, "UPLOAD test.txt\n", 16, 0);
    // Wait for READY or ACK, but don't spam output; gather response
    resp = recv_with_gather(sockfd, 1500);
    if (resp) {
        // print non-ACK lines
        char *dummy = NULL;
        process_and_print_response(resp, &dummy);
        free(dummy);
        free(resp);
    }

    // Send file bytes
    send(sockfd, buf, (size_t)sz, 0);
    free(buf);

    // Wait for task complete
    resp = recv_with_gather(sockfd, 2500);
    char *result_payload = NULL;
    if (resp) {
        int ok = process_and_print_response(resp, &result_payload);
        if (ok && result_payload) {
            // if server attached content for some reason, show it
            printf("%s[PAYLOAD]%s %s\n", CYAN, RESET, result_payload);
            free(result_payload);
        }
        free(resp);
    }

    // LIST again
    log_action("Listing files after upload");
    send(sockfd, "LIST\n", 5, 0);
    resp = recv_with_gather(sockfd, 1500);
    if (resp) { process_and_print_response(resp, &(char*){NULL}); free(resp); }

    log_success("Test Scenario 1 finished");
    print_divider();
}

void test_scenario_2(void) {
    print_divider();
    log_info("Test Scenario 2: Multi-user quick test");

    int s1 = connect_to_server("127.0.0.1", 8080);
    if (s1 >= 0) {
        char *r = recv_with_gather(s1, 1500);
        if (r) { process_and_print_response(r, &(char*){NULL}); free(r); }
        send(s1, "SIGNUP user2 pass456\n", 21, 0);
        r = recv_with_gather(s1, 1500);
        if (r) { process_and_print_response(r, &(char*){NULL}); free(r); }
        send(s1, "LIST\n", 5, 0);
        r = recv_with_gather(s1, 1500);
        if (r) { process_and_print_response(r, &(char*){NULL}); free(r); }
        close(s1);
        log_success("User2 scenario done");
    }

    sleep(1);

    int s2 = connect_to_server("127.0.0.1", 8080);
    if (s2 >= 0) {
        char *r = recv_with_gather(s2, 1500);
        if (r) { process_and_print_response(r, &(char*){NULL}); free(r); }
        send(s2, "SIGNUP user3 pass789\n", 21, 0);
        r = recv_with_gather(s2, 1500);
        if (r) { process_and_print_response(r, &(char*){NULL}); free(r); }
        send(s2, "LIST\n", 5, 0);
        r = recv_with_gather(s2, 1500);
        if (r) { process_and_print_response(r, &(char*){NULL}); free(r); }
        close(s2);
        log_success("User3 scenario done");
    }

    print_divider();
    log_success("Test Scenario 2 finished");
}

// ------------------------- Interactive -------------------------
void interactive_mode(int sockfd) {
    print_divider();
    printf("%s=== Interactive Mode ===%s\n", CYAN, RESET);
    printf("Commands: SIGNUP/LOGIN/UPLOAD/DOWNLOAD/DELETE/LIST/QUIT\n");
    print_divider();

    char in[512];

    // Wait for initial AUTH prompt from server before user input
    char *initial = recv_with_gather(sockfd, 2000);
    if (initial) { process_and_print_response(initial, &(char*){NULL}); free(initial); }

    while (1) {
        printf("%sclient>%s ", GREEN, RESET);
        fflush(stdout);
        if (!fgets(in, sizeof(in), stdin)) break;
        in[strcspn(in, "\n")] = 0;
        char *line = trim(in);
        if (!line || !*line) continue;
        if (strcasecmp(line, "quit") == 0 || strcasecmp(line, "exit") == 0) break;

        // parse cmd
        char copy[512]; strncpy(copy, line, sizeof(copy)); copy[sizeof(copy)-1]=0;
        char *cmd = strtok(copy, " ");
        if (!cmd) continue;

        if (strcasecmp(cmd, "upload") == 0) {
            char *fname = strtok(NULL, " ");
            if (!fname) { log_error("Usage: UPLOAD <filename>"); continue; }

            // open local file (support relative path, e.g., ../test.txt)
            FILE *f = fopen(fname, "rb");
            if (!f) { log_error("Cannot open file '%s'", fname); continue; }
            fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
            char *data = malloc((size_t)sz);
            fread(data, 1, (size_t)sz, f); fclose(f);

            char cmdline[256];
            snprintf(cmdline, sizeof(cmdline), "UPLOAD %s\n", strrchr(fname, '/') ? strrchr(fname, '/')+1 : fname);
            send(sockfd, cmdline, strlen(cmdline), 0);

            // wait for READY or short ACK gather
            char *r = recv_with_gather(sockfd, 1200);
            if (r) { process_and_print_response(r, &(char*){NULL}); free(r); }
            // send data
            send(sockfd, data, (size_t)sz, 0);
            free(data);

            // wait for final result
            r = recv_with_gather(sockfd, 2000);
            char *payload = NULL;
            if (r) { process_and_print_response(r, &payload); free(r); }
            if (payload) { free(payload); }
            continue;
        }

        else if (strcasecmp(cmd, "download") == 0) {
            char *fname = strtok(NULL, " ");
            if (!fname) { log_error("Usage: DOWNLOAD <filename>"); continue; }
            char cmdline[256];
            snprintf(cmdline, sizeof(cmdline), "DOWNLOAD %s\n", fname);
            send(sockfd, cmdline, strlen(cmdline), 0);

            char *r = recv_with_gather(sockfd, 2000);
            if (!r) { log_error("No response for DOWNLOAD"); continue; }

            // If server included "TASK_COMPLETE: SUCCESS - <payload>" then save payload
            char *payload = NULL;
            int ok = process_and_print_response(r, &payload);
            if (ok && payload) {
                // save as downloaded_<fname>
                char outname[512];
                snprintf(outname, sizeof(outname), "downloaded_%s", fname);
                FILE *out = fopen(outname, "wb");
                if (out) {
                    fwrite(payload, 1, strlen(payload), out);
                    fclose(out);
                    log_success("Saved downloaded file to %s", outname);
                } else {
                    log_error("Failed to save downloaded file");
                }
                free(payload);
            }
            free(r);
            continue;
        }

        else if (strcasecmp(cmd, "delete") == 0) {
            char *fname = strtok(NULL, " ");
            if (!fname) { log_error("Usage: DELETE <filename>"); continue; }
            char cmdline[256];
            snprintf(cmdline, sizeof(cmdline), "DELETE %s\n", fname);
            send(sockfd, cmdline, strlen(cmdline), 0);
            char *r = recv_with_gather(sockfd, 1500);
            if (r) { process_and_print_response(r, &(char*){NULL}); free(r); }
            continue;
        }

        else {
            // generic command (signup, login, list, quit)
            char cmdline[512];
            snprintf(cmdline, sizeof(cmdline), "%s\n", line);
            send(sockfd, cmdline, strlen(cmdline), 0);
            char *r = recv_with_gather(sockfd, 1500);
            if (r) { process_and_print_response(r, &(char*){NULL}); free(r); }
            continue;
        }
    }

    log_info("Exiting interactive mode");
}

// ------------------------- main -------------------------
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port> [mode]\n", argv[0]);
        return 1;
    }
    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    const char *mode = (argc >= 4) ? argv[3] : "interactive";

    int sockfd = connect_to_server(server_ip, port);
    if (sockfd < 0) {
        log_error("Connection failed to server");
        return 1;
    }

    if (strcmp(mode, "test1") == 0) {
        test_scenario_1(sockfd);
    } else if (strcmp(mode, "test2") == 0) {
        close(sockfd);
        test_scenario_2();
    } else {
        interactive_mode(sockfd);
    }

    close(sockfd);
    log_info("🔒 Disconnected from server");
    return 0;
}

