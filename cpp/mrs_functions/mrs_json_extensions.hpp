#ifndef MRS_JSON_EXTENSIONS_HPP
#define MRS_JSON_EXTENSIONS_HPP

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "../sqlite3.h" // Путь на уровень выше

// --- Вспомогательная функция проверки сегментов ---
static int is_numeric_segment(const char *str, int len) {
    if (len == 0) return 0;
    for (int i = 0; i < len; i++) {
        if (!isdigit((unsigned char)str[i])) return 0;
    }
    return 1;
}

// --- Реализация mrs_json_path ---
static void mrs_json_path_native(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 1 || sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_text(context, "$", -1, SQLITE_TRANSIENT);
        return;
    }

    const char *pg_path = (const char*)sqlite3_value_text(argv[0]);
    int is_blank = 1;
    for (int i = 0; pg_path[i] != '\0'; i++) {
        if (!isspace((unsigned char)pg_path[i])) {
            is_blank = 0;
            break;
        }
    }
    if (is_blank) {
        sqlite3_result_text(context, "$", -1, SQLITE_TRANSIENT);
        return;
    }

    size_t max_len = strlen(pg_path) * 2 + 3;
    char *sqlite_path = (char*)sqlite3_malloc((int)max_len);
    if (sqlite_path == NULL) {
        sqlite3_result_error_nomem(context);
        return;
    }

    char *dst = sqlite_path;
    *dst++ = '$';
    *dst = '\0';

    const char *token = pg_path;
    const char *next_dot;
    while (token && *token != '\0') {
        next_dot = strchr(token, '.');
        int seg_len = next_dot ? (int)(next_dot - token) : (int)strlen(token);

        if (seg_len > 0) {
            if (is_numeric_segment(token, seg_len)) {
                dst += sprintf(dst, "[%.*s]", seg_len, token);
            } else {
                dst += sprintf(dst, ".%.*s", seg_len, token);
            }
        }
        token = next_dot ? next_dot + 1 : NULL;
    }

    sqlite3_result_text(context, sqlite_path, -1, SQLITE_TRANSIENT);
    sqlite3_free(sqlite_path);
}

#endif // MRS_JSON_EXTENSIONS_HPP