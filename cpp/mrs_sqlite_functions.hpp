#ifndef MRS_SQLITE_FUNCTIONS_HPP
#define MRS_SQLITE_FUNCTIONS_HPP

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <regex.h>  // Подключаем нативную библиотеку регулярных выражений POSIX
#include "sqlite3.h"

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
    strcpy(sqlite_path, "$");
    const char *token = pg_path;
    const char *next_dot;
    while (token && *token != '\0') {
        next_dot = strchr(token, '.');
        int seg_len = next_dot ? (int)(next_dot - token) : (int)strlen(token);
        if (seg_len > 0) {
            if (is_numeric_segment(token, seg_len)) {
                strcat(sqlite_path, "[");
                strncat(sqlite_path, token, seg_len);
                strcat(sqlite_path, "]");
            } else {
                strcat(sqlite_path, ".");
                strncat(sqlite_path, token, seg_len);
            }
        }
        token = next_dot ? next_dot + 1 : NULL;
    }
    sqlite3_result_text(context, sqlite_path, -1, SQLITE_TRANSIENT);
    sqlite3_free(sqlite_path);
}

// --- Реализация mrs_lower ---
static void mrs_lower_native(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 1 || sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_null(context);
        return;
    }
    const char *source = (const char*)sqlite3_value_text(argv[0]);
    if (!source) {
        sqlite3_result_null(context);
        return;
    }
    size_t len = strlen(source);
    unsigned char *result = (unsigned char*)sqlite3_malloc((int)(len + 1));
    if (result == NULL) {
        sqlite3_result_error_nomem(context);
        return;
    }
    size_t i = 0, j = 0;
    while (i < len) {
        if ((unsigned char)source[i] == 0xD0 && i + 1 < len) {
            unsigned char next = source[i + 1];
            if (next >= 0x90 && next <= 0xAF) {
                result[j++] = 0xD0;
                result[j++] = next + 0x20;
            } else if (next >= 0xB0 && next <= 0xBF) {
                result[j++] = 0xD1;
                result[j++] = next - 0x20;
            } else if (next == 0x81) {
                result[j++] = 0xD1;
                result[j++] = 0x91;
            } else {
                result[j++] = source[i];
                result[j++] = next;
            }
            i += 2;
        } else if ((unsigned char)source[i] == 0xD1 && i + 1 < len) {
            result[j++] = source[i];
            result[j++] = source[i + 1];
            i += 2;
        } else {
            result[j++] = tolower(source[i]);
            i++;
        }
    }
    result[j] = '\0';
    sqlite3_result_text(context, (const char*)result, -1, SQLITE_TRANSIENT);
    sqlite3_free(result);
}

// --- Реализация REGEXP (2 аргумента: pattern, value) ---
static void mrs_regexp_native(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 2 || sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL) {
        sqlite3_result_int(context, 0);
        return;
    }

    const char *pattern = (const char*)sqlite3_value_text(argv[0]);
    const char *value = (const char*)sqlite3_value_text(argv[1]);

    if (!pattern || !value) {
        sqlite3_result_int(context, 0);
        return;
    }

    int cflags = REG_EXTENDED | REG_NOSUB;
    const char *normalized_pattern = pattern;

    // Эмуляция Котлин-логики: проверка флага (?i) в начале регулярного выражения
    if (strncmp(pattern, "(?i)", 4) == 0) {
        cflags |= REG_ICASE;          // Добавляем флаг игнорирования регистра
        normalized_pattern = pattern + 4; // Сдвигаем указатель, отрезая префикс "(?i)"
    }

    regex_t reg;
    // Компилируем регулярное выражение POSIX
    if (regcomp(&reg, normalized_pattern, cflags) != 0) {
        sqlite3_result_error(context, "Invalid regular expression pattern", -1);
        return;
    }

    // Выполняем поиск совпадения
    int status = regexec(&reg, value, 0, NULL, 0);
    regfree(&reg); // Обязательно освобождаем нативную память структуры regex

    // Если совпадение найдено (status == 0), возвращаем 1, иначе 0
    sqlite3_result_int(context, status == 0 ? 1 : 0);
}

// --- ЕДИНАЯ ТОЧКА РЕГИСТРАЦИИ ДЛЯ DBHostObject ---
static void mrs_register_all_functions(sqlite3 *db) {
    if (db == NULL) return;
    sqlite3_create_function(db, "mrs_json_path", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, 0, mrs_json_path_native, 0, 0);
    sqlite3_create_function(db, "lower", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, 0, mrs_lower_native, 0, 0);

    // Регистрируем функцию ПОД ИМЕНЕМ "regexp" (2 аргумента)
    // Благодаря этому стандартный SQL-оператор "REGEXP" автоматически начнет работать!
    sqlite3_create_function(db, "regexp", 2, SQLITE_UTF8 | SQLITE_DETERMINISTIC, 0, mrs_regexp_native, 0, 0);
}

#endif // MRS_SQLITE_FUNCTIONS_HPP