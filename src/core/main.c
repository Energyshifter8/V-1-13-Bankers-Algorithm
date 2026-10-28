/*
 * main.c - interactive driver for the banker's algorithm
 * (OSC 10th ed., Chapter 8 Programming Project).
 *
 * Usage:  banker [-j] [-f maximum_file] [-a allocation_file] r1 r2 ... rm
 *
 * r1..rm are the available instances of each resource type. The maximum
 * file holds one line per customer, e.g. "6,4,7,3". The optional
 * allocation file (same format) sets the resources already held by each
 * customer, so a snapshot such as the example of Section 8.6.3.3 can be
 * loaded directly.
 *
 * Commands:
 *   RQ c r1 .. rm   customer c requests resources
 *   RL c r1 .. rm   customer c releases resources
 *   *               print available, maximum, allocation and need
 *
 * With -j every command is answered by exactly one JSON line holding the
 * outcome, the message and the full state (used by the interactive UI).
 */
#define _POSIX_C_SOURCE 200809L

#include "banker.h"
#include "json.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define DEFAULT_MAX_FILE "maximum.txt"

static bank_t bank;
static bank_t initial;  /* state after loading, used by RESET */
static bank_status_t last_status;

static FILE *out;              /* command output: stdout or a buffer (-j) */
static int json_mode;

/* Result of the last command, reported in JSON mode */
static const char *outcome;    /* ok, error, granted, denied, released */
static const char *reason;     /* why a request/release was denied */
static int last_customer;
static int last_vector[MAX_RESOURCES];
static int have_trial;
static bank_t trial;           /* pretend state of an unsafe request */

/* Interface from the textbook: 0 if successful and -1 if unsuccessful. */
int request_resources(int customer_num, int request[])
{
    last_status = bank_request(&bank, customer_num, request);
    return last_status == BANK_OK ? 0 : -1;
}

void release_resources(int customer_num, int release[])
{
    last_status = bank_release(&bank, customer_num, release);
}

/* ------------------------------------------------------------------ */
/* Output helpers                                                      */
/* ------------------------------------------------------------------ */

static void cmd_error(const char *fmt, ...)
{
    va_list ap;
    outcome = "error";
    fputs("error: ", out);
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
}

static void print_vector(const int v[], int m)
{
    fputc('(', out);
    for (int j = 0; j < m; j++)
        fprintf(out, j ? ", %d" : "%d", v[j]);
    fputc(')', out);
}

static int column_width(const bank_t *b)
{
    int big = 0;
    for (int j = 0; j < b->m; j++) {
        if (b->available[j] > big)
            big = b->available[j];
        for (int i = 0; i < b->n; i++)
            if (b->maximum[i][j] > big)
                big = b->maximum[i][j];
    }
    int digits = 1;
    while (big >= 10) {
        big /= 10;
        digits++;
    }
    return digits < 3 ? 3 : digits + 1;
}

static void print_header(int m, int w)
{
    for (int j = 0; j < m; j++)
        fprintf(out, "%*c", w, 'A' + j);
}

static void print_row(const int v[], int m, int w)
{
    for (int j = 0; j < m; j++)
        fprintf(out, "%*d", w, v[j]);
}

static void print_state(const bank_t *b)
{
    static const char *titles[3] = {"Maximum", "Allocation", "Need"};
    const int (*rows[3])[MAX_RESOURCES] = {b->maximum, b->allocation,
                                           b->need};
    int w = column_width(b);
    int block = w * b->m;
    int gap = 13 - block; /* every block is at least 13 columns wide */
    if (gap < 3)
        gap = 3;

    fprintf(out, "Available\n%4s", "");
    print_header(b->m, w);
    fprintf(out, "\n%4s", "");
    print_row(b->available, b->m, w);
    fprintf(out, "\n\n%4s", "");
    for (int k = 0; k < 3; k++) {
        if (k < 2)
            fprintf(out, "%*s%-*s", w - 1, "", block + gap - w + 1,
                    titles[k]);
        else
            fprintf(out, "%*s%s\n", w - 1, "", titles[k]);
    }
    fprintf(out, "%4s", "");
    for (int k = 0; k < 3; k++) {
        print_header(b->m, w);
        if (k < 2)
            fprintf(out, "%*s", gap, "");
    }
    fputc('\n', out);
    for (int i = 0; i < b->n; i++) {
        fprintf(out, "T%-3d", i);
        for (int k = 0; k < 3; k++) {
            print_row(rows[k][i], b->m, w);
            if (k < 2)
                fprintf(out, "%*s", gap, "");
        }
        fputc('\n', out);
    }
}

static void print_sequence(const int seq[], int len)
{
    fputc('<', out);
    for (int k = 0; k < len; k++)
        fprintf(out, k ? ", T%d" : "T%d", seq[k]);
    fputc('>', out);
}

static void print_trace_step(const bank_t *b, int customer, int ok,
                             const int work[], void *ctx)
{
    (void)ctx;
    fprintf(out, "  T%-3d Need ", customer);
    print_vector(b->need[customer], b->m);
    fprintf(out, " <= Work ");
    print_vector(work, b->m);
    if (ok) {
        int next[MAX_RESOURCES];
        for (int j = 0; j < b->m; j++)
            next[j] = work[j] + b->allocation[customer][j];
        fprintf(out, " ? yes -> Work = ");
        print_vector(next, b->m);
        fprintf(out, ", Finish[%d] = true\n", customer);
    } else {
        fprintf(out, " ? no\n");
    }
}

static void cmd_safe(void)
{
    int seq[MAX_CUSTOMERS], len;

    fprintf(out, "Safety algorithm: Work = Available = ");
    print_vector(bank.available, bank.m);
    fputc('\n', out);
    if (bank_is_safe(&bank, seq, &len, print_trace_step, NULL)) {
        fprintf(out, "System is in a SAFE state. Safe sequence: ");
        print_sequence(seq, len);
        fputc('\n', out);
    } else {
        int finished[MAX_CUSTOMERS] = {0};
        for (int k = 0; k < len; k++)
            finished[seq[k]] = 1;
        fprintf(out, "System is in an UNSAFE state. Cannot finish:");
        for (int i = 0; i < bank.n; i++)
            if (!finished[i])
                fprintf(out, " T%d", i);
        fputc('\n', out);
    }
}

/*
 * Find customers whose maximum claim exceeds the system total; such a
 * customer can never finish. Returns the number of warnings.
 */
static int collect_warnings(const bank_t *b, char warn[][160])
{
    int total[MAX_RESOURCES], count = 0;
    for (int j = 0; j < b->m; j++) {
        total[j] = b->available[j];
        for (int i = 0; i < b->n; i++)
            total[j] += b->allocation[i][j];
    }
    for (int i = 0; i < b->n; i++)
        for (int j = 0; j < b->m; j++)
            if (b->maximum[i][j] > total[j]) {
                snprintf(warn[count++], 160, "T%d maximum of resource %c "
                         "(%d) exceeds the system total (%d); T%d can never "
                         "finish", i, 'A' + j, b->maximum[i][j], total[j], i);
                break;
            }
    return count;
}

static const char *status_name(bank_status_t st)
{
    switch (st) {
    case BANK_OK:                return "ok";
    case BANK_ERR_CUSTOMER:      return "customer";
    case BANK_ERR_NEGATIVE:      return "negative";
    case BANK_ERR_EXCEEDS_NEED:  return "exceeds_need";
    case BANK_ERR_UNAVAILABLE:   return "unavailable";
    case BANK_ERR_UNSAFE:        return "unsafe";
    case BANK_ERR_EXCEEDS_ALLOC: return "exceeds_allocation";
    }
    return "unknown";
}

/* ------------------------------------------------------------------ */
/* Command handling                                                    */
/* ------------------------------------------------------------------ */

static void print_help(void)
{
    fprintf(out, "Commands:\n"
           "  RQ c r1 .. rm   customer c requests resources\n"
           "  RL c r1 .. rm   customer c releases resources\n"
           "  *               print available, maximum, allocation, need\n"
           "  SAFE            run the safety algorithm step by step\n"
           "  RESET           restore the initial state\n"
           "Editing:\n"
           "  NEW n m         empty state with n customers, m resources\n"
           "  AVAIL r1 .. rm  set the available vector\n"
           "  MAX c r1 .. rm  set the maximum demand of customer c\n"
           "  ALLOC c r1 ..rm set the allocation of customer c\n"
           "  SAVE            use the current state for RESET\n"
           "  HELP            show this help\n"
           "  QUIT            exit the program\n");
}

/* Parse "c r1 .. rm" into customer and vector. Returns 0 on success. */
static int parse_customer_vector(const char *cmd, char *args, int *customer,
                                 int v[])
{
    char err[256];
    char *end;

    while (isspace((unsigned char)*args))
        args++;
    errno = 0;
    long c = strtol(args, &end, 10);
    if (end == args || (*end && !isspace((unsigned char)*end))) {
        cmd_error("%s expects a customer number and %d resource "
               "values, e.g. %s 0", cmd, bank.m, cmd);
        for (int j = 0; j < bank.m; j++)
            fprintf(out, " 1");
        fputc('\n', out);
        return -1;
    }
    if (c < 0 || c >= bank.n) {
        cmd_error("customer %ld does not exist (valid: 0..%d)\n", c,
               bank.n - 1);
        return -1;
    }

    int count = bank_parse_vector(end, v, MAX_RESOURCES, err, sizeof(err));
    if (count < 0) {
        cmd_error("%s\n", err);
        return -1;
    }
    if (count != bank.m) {
        cmd_error("%s expects %d resource values, got %d\n", cmd, bank.m,
               count);
        return -1;
    }
    *customer = (int)c;
    return 0;
}

static void cmd_request(char *args)
{
    int c, v[MAX_RESOURCES];
    if (parse_customer_vector("RQ", args, &c, v) < 0)
        return;
    last_customer = c;
    memcpy(last_vector, v, sizeof(v));

    if (request_resources(c, v) == 0) {
        outcome = "granted";
        fprintf(out, "Request GRANTED for T%d: ", c);
        print_vector(v, bank.m);
        fputc('\n', out);
    } else {
        outcome = "denied";
        reason = status_name(last_status);
        fprintf(out, "Request DENIED for T%d: ", c);
        print_vector(v, bank.m);
        fprintf(out, " - %s\n", bank_status_str(last_status));

        if (last_status == BANK_ERR_UNSAFE) {
            /* keep the pretend state so the UI can show why it is unsafe */
            trial = bank;
            for (int j = 0; j < bank.m; j++) {
                trial.available[j] -= v[j];
                trial.allocation[c][j] += v[j];
                trial.need[c][j] -= v[j];
            }
            have_trial = 1;
        }
    }
}

static void cmd_release(char *args)
{
    int c, v[MAX_RESOURCES];
    if (parse_customer_vector("RL", args, &c, v) < 0)
        return;
    last_customer = c;
    memcpy(last_vector, v, sizeof(v));

    release_resources(c, v);
    if (last_status == BANK_OK) {
        outcome = "released";
        fprintf(out, "Released ");
        print_vector(v, bank.m);
        fprintf(out, " from T%d\n", c);
    } else {
        outcome = "denied";
        reason = status_name(last_status);
        fprintf(out, "Release DENIED for T%d: ", c);
        print_vector(v, bank.m);
        fprintf(out, " - %s\n", bank_status_str(last_status));
    }
}

/* ------------------------------------------------------------------ */
/* Editing commands (used by the interactive UI and test scripts)      */
/* ------------------------------------------------------------------ */

/* NEW n m: start an empty state with n customers and m resource types */
static void cmd_new(char *args)
{
    int v[2];
    char err[256];
    int count = bank_parse_vector(args, v, 2, err, sizeof(err));
    if (count != 2) {
        cmd_error("NEW expects the number of customers and resource "
               "types, e.g. NEW 5 3\n");
        return;
    }
    if (v[0] < 1 || v[0] > MAX_CUSTOMERS || v[1] < 1 ||
        v[1] > MAX_RESOURCES) {
        cmd_error("NEW needs 1..%d customers and 1..%d resource "
               "types\n", MAX_CUSTOMERS, MAX_RESOURCES);
        return;
    }
    bank_init(&bank, v[0], v[1]);
    fprintf(out, "New empty state: %d customers, %d resource types\n", v[0],
           v[1]);
}

/* AVAIL r1 .. rm: set the available vector */
static void cmd_avail(char *args)
{
    int v[MAX_RESOURCES];
    char err[256];
    int count = bank_parse_vector(args, v, MAX_RESOURCES, err, sizeof(err));
    if (count < 0) {
        cmd_error("%s\n", err);
        return;
    }
    if (count != bank.m) {
        cmd_error("AVAIL expects %d resource values, got %d\n", bank.m,
               count);
        return;
    }
    memcpy(bank.available, v, sizeof(int) * bank.m);
    fprintf(out, "Available = ");
    print_vector(v, bank.m);
    fputc('\n', out);
}

/* MAX c r1 .. rm / ALLOC c r1 .. rm: set one row of a matrix */
static void cmd_set_row(char *args, int is_max)
{
    const char *cmd = is_max ? "MAX" : "ALLOC";
    int c, v[MAX_RESOURCES];
    if (parse_customer_vector(cmd, args, &c, v) < 0)
        return;

    for (int j = 0; j < bank.m; j++) {
        int max = is_max ? v[j] : bank.maximum[c][j];
        int alloc = is_max ? bank.allocation[c][j] : v[j];
        if (alloc > max) {
            cmd_error("T%d allocation of %c (%d) would exceed its "
                   "maximum (%d)\n", c, 'A' + j, alloc, max);
            return;
        }
    }
    memcpy(is_max ? bank.maximum[c] : bank.allocation[c], v,
           sizeof(int) * bank.m);
    bank_update_need(&bank);
    fprintf(out, "%s[T%d] = ", is_max ? "Maximum" : "Allocation", c);
    print_vector(v, bank.m);
    fputc('\n', out);
}

/* Returns 0 to continue, 1 to quit. */
static int run_command(char *line)
{
    char *s = line;
    while (isspace((unsigned char)*s))
        s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
    if (*s == '\0' || *s == '#')
        return 0;

    char *args = s;
    while (*args && !isspace((unsigned char)*args))
        args++;
    if (*args)
        *args++ = '\0';

    if (strcasecmp(s, "RQ") == 0)
        cmd_request(args);
    else if (strcasecmp(s, "RL") == 0)
        cmd_release(args);
    else if (strcmp(s, "*") == 0)
        print_state(&bank);
    else if (strcasecmp(s, "SAFE") == 0)
        cmd_safe();
    else if (strcasecmp(s, "RESET") == 0) {
        bank = initial;
        fprintf(out, "State reset to the initial values\n");
    } else if (strcasecmp(s, "SAVE") == 0) {
        initial = bank;
        fprintf(out, "Current state saved as the initial state for RESET\n");
    } else if (strcasecmp(s, "NEW") == 0)
        cmd_new(args);
    else if (strcasecmp(s, "AVAIL") == 0)
        cmd_avail(args);
    else if (strcasecmp(s, "MAX") == 0)
        cmd_set_row(args, 1);
    else if (strcasecmp(s, "ALLOC") == 0)
        cmd_set_row(args, 0);
    else if (strcasecmp(s, "HELP") == 0 || strcmp(s, "?") == 0)
        print_help();
    else if (strcasecmp(s, "QUIT") == 0 || strcasecmp(s, "EXIT") == 0)
        return 1;
    else
        cmd_error("unknown command '%s'. Type HELP for the list of "
               "commands.\n", s);
    return 0;
}

/* ------------------------------------------------------------------ */
/* JSON mode                                                           */
/* ------------------------------------------------------------------ */

/* One JSON line describing the result of a command and the new state. */
static void json_response(const char *command, const char *message)
{
    char warn[MAX_CUSTOMERS][160];
    int nwarn = collect_warnings(&bank, warn);

    fputs("{\"command\":", stdout);
    if (command != NULL)
        json_string(stdout, command);
    else
        fputs("null", stdout);
    fputs(",\"outcome\":", stdout);
    json_string(stdout, outcome);
    fputs(",\"reason\":", stdout);
    if (reason != NULL)
        json_string(stdout, reason);
    else
        fputs("null", stdout);
    if (last_customer >= 0) {
        fprintf(stdout, ",\"customer\":%d,\"vector\":", last_customer);
        json_ints(stdout, last_vector, bank.m);
    }
    fputs(",\"message\":", stdout);
    json_string(stdout, message);
    fputs(",\"state\":", stdout);
    json_state(stdout, &bank);
    fputs(",\"safety\":", stdout);
    json_safety(stdout, &bank);
    fputs(",\"trial\":", stdout);
    if (have_trial) {
        fputs("{\"state\":", stdout);
        json_state(stdout, &trial);
        fputs(",\"safety\":", stdout);
        json_safety(stdout, &trial);
        fputc('}', stdout);
    } else {
        fputs("null", stdout);
    }
    fputs(",\"warnings\":[", stdout);
    for (int k = 0; k < nwarn; k++) {
        if (k)
            fputc(',', stdout);
        json_string(stdout, warn[k]);
    }
    fputs("]}\n", stdout);
    fflush(stdout);
}

static void reset_result(void)
{
    outcome = "ok";
    reason = NULL;
    last_customer = -1;
    have_trial = 0;
}

/* Run one command in JSON mode, capturing its text output. */
static int run_json_command(char *line)
{
    char command[1024];
    char *buf = NULL;
    size_t size = 0;

    line[strcspn(line, "\r\n")] = '\0';
    snprintf(command, sizeof(command), "%s", line);

    reset_result();
    out = open_memstream(&buf, &size);
    if (out == NULL) {
        perror("open_memstream");
        return 1;
    }
    int quit = run_command(line);
    fclose(out);
    json_response(command, buf);
    free(buf);
    return quit;
}

/* ------------------------------------------------------------------ */
/* Start-up                                                            */
/* ------------------------------------------------------------------ */

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [-j] [-f maximum_file] [-a allocation_file] "
            "r1 r2 ... rm\n"
            "  r1..rm           available instances of each resource type\n"
            "  -f maximum_file  maximum demand of each customer, one line\n"
            "                   per customer (default: %s)\n"
            "  -a alloc_file    resources already allocated to each customer\n"
            "                   (optional, default: all zero)\n"
            "  -j               JSON output, one line per command (for the UI)\n"
            "Example: %s -f maximum.txt 10 5 7 8\n",
            prog, DEFAULT_MAX_FILE, prog);
}

int main(int argc, char *argv[])
{
    const char *max_file = DEFAULT_MAX_FILE;
    const char *alloc_file = NULL;
    int resources[MAX_RESOURCES];
    int m = 0;
    char err[512];

    for (int k = 1; k < argc; k++) {
        const char *a = argv[k];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(a, "-j") == 0) {
            json_mode = 1;
        } else if (strcmp(a, "-f") == 0) {
            if (++k == argc) {
                fprintf(stderr, "error: -f needs a file name\n");
                return 1;
            }
            max_file = argv[k];
        } else if (strcmp(a, "-a") == 0) {
            if (++k == argc) {
                fprintf(stderr, "error: -a needs a file name\n");
                return 1;
            }
            alloc_file = argv[k];
        } else {
            char *end;
            errno = 0;
            long v = strtol(a, &end, 10);
            if (end == a || *end != '\0') {
                fprintf(stderr, "error: '%s' is not a valid resource "
                        "count\n", a);
                usage(argv[0]);
                return 1;
            }
            if (v < 0 || v > MAX_VALUE || errno == ERANGE) {
                fprintf(stderr, "error: resource count %s must be between "
                        "0 and %d\n", a, MAX_VALUE);
                return 1;
            }
            if (m == MAX_RESOURCES) {
                fprintf(stderr, "error: too many resource types (max %d)\n",
                        MAX_RESOURCES);
                return 1;
            }
            resources[m++] = (int)v;
        }
    }

    if (m == 0) {
        fprintf(stderr, "error: no resource counts given\n");
        usage(argv[0]);
        return 1;
    }

    bank_init(&bank, 0, m);
    int n = bank_load_matrix(max_file, bank.maximum, m, err, sizeof(err));
    if (n < 0) {
        fprintf(stderr, "error: %s\n", err);
        return 1;
    }
    bank.n = n;
    memcpy(bank.available, resources, sizeof(int) * m);

    if (alloc_file != NULL) {
        int rows = bank_load_matrix(alloc_file, bank.allocation, m, err,
                                    sizeof(err));
        if (rows < 0) {
            fprintf(stderr, "error: %s\n", err);
            return 1;
        }
        if (rows != n) {
            fprintf(stderr, "error: %s has %d customers but %s has %d\n",
                    alloc_file, rows, max_file, n);
            return 1;
        }
        if (bank_validate(&bank, err, sizeof(err)) < 0) {
            fprintf(stderr, "error: %s\n", err);
            return 1;
        }
    }
    bank_update_need(&bank);
    initial = bank;

    char line[1024];
    if (json_mode) {
        reset_result();
        outcome = "ready";
        json_response(NULL, "");
        while (fgets(line, sizeof(line), stdin) != NULL)
            if (run_json_command(line))
                break;
        return 0;
    }

    out = stdout;
    char warn[MAX_CUSTOMERS][160];
    int nwarn = collect_warnings(&bank, warn);
    for (int k = 0; k < nwarn; k++)
        fprintf(stderr, "warning: %s\n", warn[k]);
    if (!bank_is_safe(&bank, NULL, NULL, NULL, NULL))
        fprintf(stderr, "warning: the initial state is UNSAFE; every request "
                "will be denied (type SAFE for details)\n");

    int interactive = isatty(STDIN_FILENO);
    if (interactive)
        printf("Banker's algorithm: %d customers, %d resource types. "
               "Type HELP for commands.\n", bank.n, bank.m);

    for (;;) {
        if (interactive) {
            printf("banker> ");
            fflush(stdout);
        }
        if (fgets(line, sizeof(line), stdin) == NULL)
            break;
        if (!interactive)
            printf("banker> %s%s", line,
                   strchr(line, '\n') ? "" : "\n");
        if (run_command(line))
            break;
        fflush(stdout);
    }
    return 0;
}
