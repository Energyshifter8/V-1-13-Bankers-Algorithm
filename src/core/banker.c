/*
 * banker.c - Banker's algorithm (OSC 10th ed., Section 8.6.3).
 */
#include "banker.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void bank_init(bank_t *b, int n, int m)
{
    memset(b, 0, sizeof(*b));
    b->n = n;
    b->m = m;
}

/* need[i][j] = maximum[i][j] - allocation[i][j] */
void bank_update_need(bank_t *b)
{
    for (int i = 0; i < b->n; i++)
        for (int j = 0; j < b->m; j++)
            b->need[i][j] = b->maximum[i][j] - b->allocation[i][j];
}

/*
 * Parse a list of non-negative integers separated by commas and/or
 * whitespace, e.g. "6,4,7,3" or "6 4 7 3".
 * Returns the number of values read, or -1 on error (message in err).
 */
int bank_parse_vector(const char *text, int out[], int max_count,
                      char *err, size_t errlen)
{
    int count = 0;
    const char *p = text;

    for (;;) {
        while (*p == ',' || isspace((unsigned char)*p))
            p++;
        if (*p == '\0')
            break;

        char *end;
        errno = 0;
        long v = strtol(p, &end, 10);
        if (end == p || (*end != '\0' && *end != ',' &&
                         !isspace((unsigned char)*end))) {
            int len = 0;
            while (p[len] && p[len] != ',' && !isspace((unsigned char)p[len]))
                len++;
            snprintf(err, errlen, "invalid number '%.*s'", len, p);
            return -1;
        }
        if (v < 0) {
            snprintf(err, errlen, "value %ld is negative", v);
            return -1;
        }
        if (errno == ERANGE || v > MAX_VALUE) {
            snprintf(err, errlen, "value is too large (max %d)", MAX_VALUE);
            return -1;
        }
        if (count == max_count) {
            snprintf(err, errlen, "too many values (expected %d)", max_count);
            return -1;
        }
        out[count++] = (int)v;
        p = end;
    }
    return count;
}

/*
 * Load a matrix file: one customer per line, m values per line.
 * Empty lines and lines starting with '#' are ignored.
 * Returns the number of rows read, or -1 on error.
 */
int bank_load_matrix(const char *path, int matrix[][MAX_RESOURCES], int m,
                     char *err, size_t errlen)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        snprintf(err, errlen, "cannot open '%s': %s", path, strerror(errno));
        return -1;
    }

    char line[1024], msg[256];
    int rows = 0, lineno = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        lineno++;
        char *s = line;
        while (isspace((unsigned char)*s))
            s++;
        if (*s == '\0' || *s == '#')
            continue;

        if (rows == MAX_CUSTOMERS) {
            snprintf(err, errlen, "%s: too many customers (max %d)",
                     path, MAX_CUSTOMERS);
            fclose(fp);
            return -1;
        }

        int count = bank_parse_vector(s, matrix[rows], MAX_RESOURCES,
                                      msg, sizeof(msg));
        if (count < 0) {
            snprintf(err, errlen, "%s:%d: %s", path, lineno, msg);
            fclose(fp);
            return -1;
        }
        if (count != m) {
            snprintf(err, errlen, "%s:%d: expected %d values, found %d",
                     path, lineno, m, count);
            fclose(fp);
            return -1;
        }
        rows++;
    }
    fclose(fp);

    if (rows == 0) {
        snprintf(err, errlen, "%s: file contains no customers", path);
        return -1;
    }
    return rows;
}

/*
 * Check that the state is consistent: allocation <= maximum for every
 * customer. Returns 0 when valid, -1 otherwise.
 */
int bank_validate(const bank_t *b, char *err, size_t errlen)
{
    for (int i = 0; i < b->n; i++)
        for (int j = 0; j < b->m; j++)
            if (b->allocation[i][j] > b->maximum[i][j]) {
                snprintf(err, errlen,
                         "T%d: allocation of resource %c (%d) exceeds "
                         "its maximum (%d)", i, 'A' + j,
                         b->allocation[i][j], b->maximum[i][j]);
                return -1;
            }
    return 0;
}

/* Return 1 when a[j] <= b[j] for every j < m. */
static int vec_leq(const int a[], const int b[], int m)
{
    for (int j = 0; j < m; j++)
        if (a[j] > b[j])
            return 0;
    return 1;
}

/*
 * Safety algorithm (Section 8.6.3.1).
 *
 *  1. Work = Available, Finish[i] = false for all i.
 *  2. Find an i such that Finish[i] == false and Need_i <= Work.
 *     If no such i exists, go to step 4.
 *  3. Work = Work + Allocation_i, Finish[i] = true. Go to step 2.
 *  4. If Finish[i] == true for all i, the system is in a safe state.
 *
 * The customers are scanned in order 0..n-1 repeatedly until a full pass
 * makes no progress. The order in which customers finish is stored in
 * sequence[] (its length in *seq_len). Returns 1 if safe, 0 if unsafe.
 * trace (may be NULL) is called for every examined customer.
 */
int bank_is_safe(const bank_t *b, int sequence[], int *seq_len,
                 bank_trace_fn trace, void *ctx)
{
    int work[MAX_RESOURCES];
    int finish[MAX_CUSTOMERS] = {0};
    int done = 0, progress = 1;

    memcpy(work, b->available, sizeof(work));

    while (progress && done < b->n) {
        progress = 0;
        for (int i = 0; i < b->n; i++) {
            if (finish[i])
                continue;
            int ok = vec_leq(b->need[i], work, b->m);
            if (trace != NULL)
                trace(b, i, ok, work, ctx);
            if (!ok)
                continue;
            for (int j = 0; j < b->m; j++)
                work[j] += b->allocation[i][j];
            finish[i] = 1;
            if (sequence != NULL)
                sequence[done] = i;
            done++;
            progress = 1;
        }
    }

    if (seq_len != NULL)
        *seq_len = done;
    return done == b->n;
}
