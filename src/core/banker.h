/*
 * banker.h - Banker's algorithm (OSC 10th ed., Section 8.6.3).
 *
 * The bank keeps track of n customers and m resource types using the
 * four data structures described in the Programming Project of Chapter 8:
 * available, maximum, allocation and need.
 */
#ifndef BANKER_H
#define BANKER_H

#define MAX_CUSTOMERS 32
#define MAX_RESOURCES 16
#define MAX_VALUE     1000000

typedef struct {
    int n; /* number of customers (threads) */
    int m; /* number of resource types */

    /* the available amount of each resource */
    int available[MAX_RESOURCES];
    /* the maximum demand of each customer */
    int maximum[MAX_CUSTOMERS][MAX_RESOURCES];
    /* the amount currently allocated to each customer */
    int allocation[MAX_CUSTOMERS][MAX_RESOURCES];
    /* the remaining need of each customer */
    int need[MAX_CUSTOMERS][MAX_RESOURCES];
} bank_t;

typedef enum {
    BANK_OK = 0,
    BANK_ERR_CUSTOMER,      /* customer number out of range           */
    BANK_ERR_NEGATIVE,      /* negative value in a request/release    */
    BANK_ERR_EXCEEDS_NEED,  /* request > need (exceeds maximum claim) */
    BANK_ERR_UNAVAILABLE,   /* request > available: customer must wait */
    BANK_ERR_UNSAFE,        /* granting would leave an unsafe state   */
    BANK_ERR_EXCEEDS_ALLOC  /* release > allocation                   */
} bank_status_t;

/*
 * Called by the safety algorithm every time it examines a customer.
 * ok == 1 when need[customer] <= work, i.e. the customer can finish.
 * work is the Work vector *before* the customer's allocation is added.
 */
typedef void (*bank_trace_fn)(const bank_t *b, int customer, int ok,
                              const int work[], void *ctx);

#include <stddef.h>

/* Initialisation and input */
void bank_init(bank_t *b, int n, int m);
void bank_update_need(bank_t *b);
int  bank_parse_vector(const char *text, int out[], int max_count,
                       char *err, size_t errlen);
int  bank_load_matrix(const char *path, int matrix[][MAX_RESOURCES], int m,
                      char *err, size_t errlen);
int  bank_validate(const bank_t *b, char *err, size_t errlen);

/* Safety algorithm (Section 8.6.3.1) */
int  bank_is_safe(const bank_t *b, int sequence[], int *seq_len,
                  bank_trace_fn trace, void *ctx);

/* Resource-request algorithm (Section 8.6.3.2) */
bank_status_t bank_request(bank_t *b, int customer, const int request[]);

#endif /* BANKER_H */
