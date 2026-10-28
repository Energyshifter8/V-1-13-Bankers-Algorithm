/*
 * json.c - minimal JSON output for the banker state (used by the UI).
 */
#include "json.h"

void json_string(FILE *fp, const char *s)
{
    fputc('"', fp);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  fputs("\\\"", fp); break;
        case '\\': fputs("\\\\", fp); break;
        case '\n': fputs("\\n", fp); break;
        case '\r': fputs("\\r", fp); break;
        case '\t': fputs("\\t", fp); break;
        default:
            if (c < 0x20)
                fprintf(fp, "\\u%04x", c);
            else
                fputc(c, fp);
        }
    }
    fputc('"', fp);
}

void json_ints(FILE *fp, const int v[], int count)
{
    fputc('[', fp);
    for (int j = 0; j < count; j++)
        fprintf(fp, j ? ",%d" : "%d", v[j]);
    fputc(']', fp);
}

static void json_matrix(FILE *fp, const int a[][MAX_RESOURCES], int n, int m)
{
    fputc('[', fp);
    for (int i = 0; i < n; i++) {
        if (i)
            fputc(',', fp);
        json_ints(fp, a[i], m);
    }
    fputc(']', fp);
}

/* {"n":..,"m":..,"available":[..],"maximum":[[..]],"allocation":..,
 *  "need":..,"total":[..]} */
void json_state(FILE *fp, const bank_t *b)
{
    int total[MAX_RESOURCES];
    for (int j = 0; j < b->m; j++) {
        total[j] = b->available[j];
        for (int i = 0; i < b->n; i++)
            total[j] += b->allocation[i][j];
    }

    fprintf(fp, "{\"n\":%d,\"m\":%d,\"available\":", b->n, b->m);
    json_ints(fp, b->available, b->m);
    fputs(",\"maximum\":", fp);
    json_matrix(fp, b->maximum, b->n, b->m);
    fputs(",\"allocation\":", fp);
    json_matrix(fp, b->allocation, b->n, b->m);
    fputs(",\"need\":", fp);
    json_matrix(fp, b->need, b->n, b->m);
    fputs(",\"total\":", fp);
    json_ints(fp, total, b->m);
    fputc('}', fp);
}

typedef struct {
    FILE *fp;
    int count;
} trace_ctx_t;

static void json_step(const bank_t *b, int customer, int ok,
                      const int work[], void *ctx)
{
    trace_ctx_t *t = ctx;
    if (t->count++)
        fputc(',', t->fp);
    fprintf(t->fp, "{\"customer\":%d,\"ok\":%s,\"work\":", customer,
            ok ? "true" : "false");
    json_ints(t->fp, work, b->m);
    if (ok) {
        int next[MAX_RESOURCES];
        for (int j = 0; j < b->m; j++)
            next[j] = work[j] + b->allocation[customer][j];
        fputs(",\"next\":", t->fp);
        json_ints(t->fp, next, b->m);
    }
    fputc('}', t->fp);
}

/* {"steps":[{customer,ok,work,next?}..],"safe":bool,"sequence":[..]} */
void json_safety(FILE *fp, const bank_t *b)
{
    int seq[MAX_CUSTOMERS], len;
    trace_ctx_t ctx = {fp, 0};

    fputs("{\"steps\":[", fp);
    int safe = bank_is_safe(b, seq, &len, json_step, &ctx);
    fprintf(fp, "],\"safe\":%s,\"sequence\":", safe ? "true" : "false");
    json_ints(fp, seq, len);
    fputc('}', fp);
}
