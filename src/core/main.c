/*
 * main.c - interactive driver for the banker's algorithm
 * (OSC 10th ed., Chapter 8 Programming Project).
 *
 * Usage:  banker [-f maximum_file] r1 r2 ... rm
 *
 * r1..rm are the available instances of each resource type. The maximum
 * file holds one line per customer, e.g. "6,4,7,3".
 *
 * Commands:
 *   RQ c r1 .. rm   customer c requests resources
 *   RL c r1 .. rm   customer c releases resources
 *   *               print available, maximum, allocation and need
 */
#define _POSIX_C_SOURCE 200809L

#include "banker.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define DEFAULT_MAX_FILE "maximum.txt"

static bank_t bank;
static bank_t initial;  /* state after loading, used by RESET */
static bank_status_t last_status;

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

static void print_vector(const int v[], int m)
{
    putchar('(');
    for (int j = 0; j < m; j++)
        printf(j ? ", %d" : "%d", v[j]);
    putchar(')');
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
        printf("%*c", w, 'A' + j);
}

static void print_row(const int v[], int m, int w)
{
    for (int j = 0; j < m; j++)
        printf("%*d", w, v[j]);
}

static void print_state(const bank_t *b)
{
    int w = column_width(b);
    int block = w * b->m;

    printf("Available\n%4s", "");
    print_header(b->m, w);
    printf("\n%4s", "");
    print_row(b->available, b->m, w);
    printf("\n\n%4s", "");
    printf("%-*s   %-*s   %s\n", block, "  Maximum", block, "  Allocation",
           "  Need");
    printf("%4s", "");
    for (int k = 0; k < 3; k++) {
        print_header(b->m, w);
        printf(k < 2 ? "   " : "\n");
    }
    for (int i = 0; i < b->n; i++) {
        printf("T%-3d", i);
        print_row(b->maximum[i], b->m, w);
        printf("   ");
        print_row(b->allocation[i], b->m, w);
        printf("   ");
        print_row(b->need[i], b->m, w);
        putchar('\n');
    }
}

/* Warn about customers whose maximum claim exceeds the system total. */
static void check_total(const bank_t *b)
{
    int total[MAX_RESOURCES];
    for (int j = 0; j < b->m; j++) {
        total[j] = b->available[j];
        for (int i = 0; i < b->n; i++)
            total[j] += b->allocation[i][j];
    }
    for (int i = 0; i < b->n; i++)
        for (int j = 0; j < b->m; j++)
            if (b->maximum[i][j] > total[j]) {
                fprintf(stderr, "warning: T%d maximum of resource %c (%d) "
                        "exceeds the system total (%d); T%d can never "
                        "finish\n", i, 'A' + j, b->maximum[i][j], total[j],
                        i);
                break;
            }
}

/* ------------------------------------------------------------------ */
/* Command handling                                                    */
/* ------------------------------------------------------------------ */

static void print_help(void)
{
    printf("Commands:\n"
           "  RQ c r1 .. rm   customer c requests resources\n"
           "  RL c r1 .. rm   customer c releases resources\n"
           "  *               print available, maximum, allocation, need\n"
           "  RESET           restore the initial state\n"
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
        printf("error: %s expects a customer number and %d resource "
               "values, e.g. %s 0", cmd, bank.m, cmd);
        for (int j = 0; j < bank.m; j++)
            printf(" 1");
        putchar('\n');
        return -1;
    }
    if (c < 0 || c >= bank.n) {
        printf("error: customer %ld does not exist (valid: 0..%d)\n", c,
               bank.n - 1);
        return -1;
    }

    int count = bank_parse_vector(end, v, MAX_RESOURCES, err, sizeof(err));
    if (count < 0) {
        printf("error: %s\n", err);
        return -1;
    }
    if (count != bank.m) {
        printf("error: %s expects %d resource values, got %d\n", cmd, bank.m,
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

    if (request_resources(c, v) == 0) {
        printf("Request GRANTED for T%d: ", c);
        print_vector(v, bank.m);
        putchar('\n');
    } else {
        printf("Request DENIED for T%d: ", c);
        print_vector(v, bank.m);
        printf(" - %s\n", bank_status_str(last_status));
    }
}

static void cmd_release(char *args)
{
    int c, v[MAX_RESOURCES];
    if (parse_customer_vector("RL", args, &c, v) < 0)
        return;

    release_resources(c, v);
    if (last_status == BANK_OK) {
        printf("Released ");
        print_vector(v, bank.m);
        printf(" from T%d\n", c);
    } else {
        printf("Release DENIED for T%d: ", c);
        print_vector(v, bank.m);
        printf(" - %s\n", bank_status_str(last_status));
    }
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
    else if (strcasecmp(s, "RESET") == 0) {
        bank = initial;
        printf("State reset to the initial values\n");
    } else if (strcasecmp(s, "HELP") == 0 || strcmp(s, "?") == 0)
        print_help();
    else if (strcasecmp(s, "QUIT") == 0 || strcasecmp(s, "EXIT") == 0)
        return 1;
    else
        printf("error: unknown command '%s'. Type HELP for the list of "
               "commands.\n", s);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Start-up                                                            */
/* ------------------------------------------------------------------ */

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [-f maximum_file] r1 r2 ... rm\n"
            "  r1..rm           available instances of each resource type\n"
            "  -f maximum_file  maximum demand of each customer, one line\n"
            "                   per customer (default: %s)\n"
            "Example: %s -f maximum.txt 10 5 7 8\n",
            prog, DEFAULT_MAX_FILE, prog);
}

int main(int argc, char *argv[])
{
    const char *max_file = DEFAULT_MAX_FILE;
    int resources[MAX_RESOURCES];
    int m = 0;
    char err[512];

    for (int k = 1; k < argc; k++) {
        const char *a = argv[k];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(a, "-f") == 0) {
            if (++k == argc) {
                fprintf(stderr, "error: -f needs a file name\n");
                return 1;
            }
            max_file = argv[k];
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
    bank_update_need(&bank);
    initial = bank;

    check_total(&bank);

    int interactive = isatty(STDIN_FILENO);
    if (interactive)
        printf("Banker's algorithm: %d customers, %d resource types. "
               "Type HELP for commands.\n", bank.n, bank.m);

    char line[1024];
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
