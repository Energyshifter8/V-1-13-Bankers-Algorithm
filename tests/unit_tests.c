/*
 * unit_tests.c - unit tests for the banker's algorithm core.
 * Build and run with: make test
 */
#include "../src/core/banker.h"

#include <stdio.h>
#include <string.h>

static int failures = 0, checks = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        checks++;                                                       \
        if (!(cond)) {                                                  \
            failures++;                                                 \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                               \
    } while (0)

static void set_rows(int dst[][MAX_RESOURCES], const int *src, int n, int m)
{
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            dst[i][j] = src[i * m + j];
}

/* Snapshot of Section 8.6.3.3 */
static void book_example(bank_t *b)
{
    static const int max[] = {7, 5, 3, 3, 2, 2, 9, 0, 2, 2, 2, 2, 4, 3, 3};
    static const int alloc[] = {0, 1, 0, 2, 0, 0, 3, 0, 2, 2, 1, 1, 0, 0, 2};

    bank_init(b, 5, 3);
    set_rows(b->maximum, max, 5, 3);
    set_rows(b->allocation, alloc, 5, 3);
    b->available[0] = 3;
    b->available[1] = 3;
    b->available[2] = 2;
    bank_update_need(b);
}

static void test_need(void)
{
    bank_t b;
    book_example(&b);
    /* Need column from the textbook */
    static const int need[] = {7, 4, 3, 1, 2, 2, 6, 0, 0, 0, 1, 1, 4, 3, 1};
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 3; j++)
            CHECK(b.need[i][j] == need[i * 3 + j]);
}

static void test_safe_book_example(void)
{
    bank_t b;
    int seq[MAX_CUSTOMERS], len = -1;
    book_example(&b);
    CHECK(bank_is_safe(&b, seq, &len, NULL, NULL) == 1);
    CHECK(len == 5);
    /* Scanning in order gives <T1, T3, T4, T0, T2> */
    int expected[] = {1, 3, 4, 0, 2};
    CHECK(memcmp(seq, expected, sizeof(expected)) == 0);
}

static void test_request_sequence_book(void)
{
    bank_t b;
    book_example(&b);

    /* T1 requests (1,0,2): granted, Available becomes (2,3,0) */
    int r1[] = {1, 0, 2};
    CHECK(bank_request(&b, 1, r1) == BANK_OK);
    CHECK(b.available[0] == 2 && b.available[1] == 3 && b.available[2] == 0);
    CHECK(b.allocation[1][0] == 3 && b.allocation[1][2] == 2);
    CHECK(b.need[1][0] == 0 && b.need[1][1] == 2 && b.need[1][2] == 0);

    /* T4 requests (3,3,0): resources not available */
    int r2[] = {3, 3, 0};
    CHECK(bank_request(&b, 4, r2) == BANK_ERR_UNAVAILABLE);

    /* T0 requests (0,2,0): available but unsafe; state must be unchanged */
    bank_t before = b;
    int r3[] = {0, 2, 0};
    CHECK(bank_request(&b, 0, r3) == BANK_ERR_UNSAFE);
    CHECK(memcmp(&before, &b, sizeof(b)) == 0);
}

static void test_request_errors(void)
{
    bank_t b;
    book_example(&b);
    int big[] = {8, 0, 0};
    int neg[] = {0, -1, 0};
    int ok[] = {0, 0, 0};
    CHECK(bank_request(&b, 0, big) == BANK_ERR_EXCEEDS_NEED);
    CHECK(bank_request(&b, 0, neg) == BANK_ERR_NEGATIVE);
    CHECK(bank_request(&b, 5, ok) == BANK_ERR_CUSTOMER);
    CHECK(bank_request(&b, -1, ok) == BANK_ERR_CUSTOMER);
    CHECK(bank_request(&b, 0, ok) == BANK_OK); /* zero request, still safe */
}

static void test_release(void)
{
    bank_t b;
    book_example(&b);
    int too_much[] = {3, 0, 0};
    int rel[] = {2, 0, 0};
    CHECK(bank_release(&b, 1, too_much) == BANK_ERR_EXCEEDS_ALLOC);
    CHECK(bank_release(&b, 1, rel) == BANK_OK);
    CHECK(b.available[0] == 5);
    CHECK(b.allocation[1][0] == 0);
    CHECK(b.need[1][0] == 3);
}

/* Section 8.6.2: 12 instances, T2 asking for one more makes it unsafe */
static void test_single_resource(void)
{
    bank_t b;
    bank_init(&b, 3, 1);
    b.maximum[0][0] = 10; b.allocation[0][0] = 5;
    b.maximum[1][0] = 4;  b.allocation[1][0] = 2;
    b.maximum[2][0] = 9;  b.allocation[2][0] = 2;
    b.available[0] = 3;
    bank_update_need(&b);

    int seq[MAX_CUSTOMERS], len;
    CHECK(bank_is_safe(&b, seq, &len, NULL, NULL) == 1);
    CHECK(seq[0] == 1 && seq[1] == 0 && seq[2] == 2);

    int one[] = {1};
    CHECK(bank_request(&b, 2, one) == BANK_ERR_UNSAFE);
    CHECK(bank_request(&b, 1, one) == BANK_OK);
}

static void test_unsafe_state(void)
{
    bank_t b;
    bank_init(&b, 2, 1);
    b.maximum[0][0] = 4; b.allocation[0][0] = 2;
    b.maximum[1][0] = 4; b.allocation[1][0] = 2;
    b.available[0] = 1;
    bank_update_need(&b);
    int len;
    CHECK(bank_is_safe(&b, NULL, &len, NULL, NULL) == 0);
    CHECK(len == 0);
}

static int trace_calls;
static void count_trace(const bank_t *b, int c, int ok, const int w[],
                        void *ctx)
{
    (void)b; (void)c; (void)ok; (void)w; (void)ctx;
    trace_calls++;
}

static void test_trace(void)
{
    bank_t b;
    book_example(&b);
    trace_calls = 0;
    bank_is_safe(&b, NULL, NULL, count_trace, NULL);
    /* pass 1: T0 no, T1 yes, T2 no, T3 yes, T4 yes; pass 2: T0, T2 yes */
    CHECK(trace_calls == 7);
}

static void test_parse_vector(void)
{
    int v[MAX_RESOURCES];
    char err[128];
    CHECK(bank_parse_vector("6,4,7,3", v, MAX_RESOURCES, err, 128) == 4);
    CHECK(v[0] == 6 && v[3] == 3);
    CHECK(bank_parse_vector(" 1  2\t3 ", v, MAX_RESOURCES, err, 128) == 3);
    CHECK(bank_parse_vector("1, 2 ,3", v, MAX_RESOURCES, err, 128) == 3);
    CHECK(bank_parse_vector("", v, MAX_RESOURCES, err, 128) == 0);
    CHECK(bank_parse_vector("1,a,3", v, MAX_RESOURCES, err, 128) == -1);
    CHECK(bank_parse_vector("1,2x", v, MAX_RESOURCES, err, 128) == -1);
    CHECK(bank_parse_vector("1,-2", v, MAX_RESOURCES, err, 128) == -1);
    CHECK(bank_parse_vector("99999999999", v, MAX_RESOURCES, err, 128) == -1);
    CHECK(bank_parse_vector("1 2 3", v, 2, err, 128) == -1);
}

static void test_load_files(void)
{
    bank_t b;
    char err[512];
    bank_init(&b, 0, 4);
    CHECK(bank_load_matrix("tests/data/book_project_max.txt", b.maximum, 4,
                           err, sizeof(err)) == 5);
    CHECK(b.maximum[4][1] == 6);
    CHECK(bank_load_matrix("tests/data/invalid/wrong_count.txt", b.maximum,
                           4, err, sizeof(err)) == -1);
    CHECK(strstr(err, ":2: expected 4 values, found 3") != NULL);
    CHECK(bank_load_matrix("tests/data/invalid/not_a_number.txt", b.maximum,
                           4, err, sizeof(err)) == -1);
    CHECK(bank_load_matrix("tests/data/invalid/negative.txt", b.maximum,
                           4, err, sizeof(err)) == -1);
    CHECK(bank_load_matrix("tests/data/invalid/empty.txt", b.maximum,
                           4, err, sizeof(err)) == -1);
    CHECK(bank_load_matrix("tests/data/no_such_file.txt", b.maximum,
                           4, err, sizeof(err)) == -1);
}

static void test_validate(void)
{
    bank_t b;
    char err[256];
    book_example(&b);
    CHECK(bank_validate(&b, err, sizeof(err)) == 0);
    b.allocation[2][0] = 10;
    CHECK(bank_validate(&b, err, sizeof(err)) == -1);
}

int main(void)
{
    struct {
        const char *name;
        void (*fn)(void);
    } tests[] = {
        {"need = max - allocation", test_need},
        {"safety: textbook 8.6.3.3", test_safe_book_example},
        {"request: textbook 8.6.3.3 sequence", test_request_sequence_book},
        {"request: invalid input", test_request_errors},
        {"release", test_release},
        {"single resource: textbook 8.6.2", test_single_resource},
        {"unsafe state", test_unsafe_state},
        {"safety trace callback", test_trace},
        {"parse vector", test_parse_vector},
        {"load matrix files", test_load_files},
        {"validate allocation <= max", test_validate},
    };
    int count = (int)(sizeof(tests) / sizeof(tests[0]));

    for (int k = 0; k < count; k++) {
        int before = failures;
        tests[k].fn();
        printf("[%s] %s\n", failures == before ? "PASS" : "FAIL",
               tests[k].name);
    }
    printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
