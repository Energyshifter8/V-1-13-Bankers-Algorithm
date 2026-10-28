/*
 * json.h - minimal JSON output for the banker state (used by the UI).
 */
#ifndef BANKER_JSON_H
#define BANKER_JSON_H

#include "banker.h"

#include <stdio.h>

void json_string(FILE *fp, const char *s);
void json_ints(FILE *fp, const int v[], int count);
void json_state(FILE *fp, const bank_t *b);
void json_safety(FILE *fp, const bank_t *b);

#endif /* BANKER_JSON_H */
