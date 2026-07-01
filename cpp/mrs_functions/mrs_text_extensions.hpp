#ifndef MRS_TEXT_EXTENSIONS_HPP
#define MRS_TEXT_EXTENSIONS_HPP

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <regex.h>
#include "../sqlite3.h"

typedef enum { MRS_TO_LOWER, MRS_TO_UPPER } MrsCaseMode;

// --- ЕДИНЫЙ ХЕЛПЕР для LOWER и UPPER (Никакого дублирования кода!) ---
static unsigned char* mrs_utf8_case_alloc(const char *source, MrsCaseMode mode) {
    if (!source) return NULL;

    size_t len = strlen(source);
    // Безопасная аллокация памяти (без переполнения int)
    unsigned char *result = (unsigned char*)sqlite3_malloc64(len + 1);
    if (result == NULL) return NULL;

    size_t i = 0, j = 0;
    while (i < len) {
        // Первый блок кириллицы (стартовый байт 0xD0)
        if ((unsigned char)source[i] == 0xD0 && i + 1 < len) {
            unsigned char next = source[i + 1];

            if (mode == MRS_TO_LOWER) {
                // А-П -> а-п
                if (next >= 0x90 && next <= 0xAF) {
                    result[j++] = 0xD0;
                    result[j++] = next + 0x20;
                }
                // Р-Я -> р-я (уходят в блок 0xD1)
                else if (next >= 0xB0 && next <= 0xBF) {
                    result[j++] = 0xD1;
                    result[j++] = next - 0x30;
                }
                // Ё -> ё
                else if (next == 0x81) {
                    result[j++] = 0xD1;
                    result[j++] = 0x91;
                }
                else {
                    result[j++] = 0xD0;
                    result[j++] = next;
                }
            }
            else { // MRS_TO_UPPER
                // а-п -> А-П
                if (next >= 0xB0 && next <= 0xBF) {
                    result[j++] = 0xD0;
                    result[j++] = next - 0x20;
                }
                else {
                    result[j++] = 0xD0;
                    result[j++] = next;
                }
            }
            i += 2;
        }
        // Второй блок кириллицы (стартовый байт 0xD1)
        else if ((unsigned char)source[i] == 0xD1 && i + 1 < len) {
            unsigned char next = source[i + 1];

            if (mode == MRS_TO_UPPER) {
                // р-я -> Р-Я (возвращаются в блок 0xD0)
                if (next >= 0x80 && next <= 0x8F) {
                    result[j++] = 0xD0;
                    result[j++] = next + 0x30;
                }
                // ё -> Ё
                else if (next == 0x91) {
                    result[j++] = 0xD0;
                    result[j++] = 0x81;
                }
                else {
                    result[j++] = 0xD1;
                    result[j++] = next;
                }
            }
            else {
                result[j++] = 0xD1;
                result[j++] = next;
            }
            i += 2;
        }
        // Стандартный ASCII (английский, цифры, знаки)
        else {
            result[j++] = (mode == MRS_TO_LOWER) ? tolower((unsigned char)source[i]) : toupper((unsigned char)source[i]);
            i++;
        }
    }
    result[j] = '\0';
    return result;
}

// --- Реализация LOWER ---
static void mrs_lower_native(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 1 || sqlite3_value_type(argv[0]) == SQLITE_NULL) { sqlite3_result_null(context); return; }
    const char *source = (const char*)sqlite3_value_text(argv[0]);
    unsigned char *result = mrs_utf8_case_alloc(source, MRS_TO_LOWER);
    if (!result) { sqlite3_result_error_nomem(context); return; }
    // Передаем деструктор sqlite3_free, чтобы SQLite сам очистил память без дублирования буфера
    sqlite3_result_text(context, (const char*)result, -1, sqlite3_free);
}

// --- Реализация UPPER ---
static void mrs_upper_native(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 1 || sqlite3_value_type(argv[0]) == SQLITE_NULL) { sqlite3_result_null(context); return; }
    const char *source = (const char*)sqlite3_value_text(argv[0]);
    unsigned char *result = mrs_utf8_case_alloc(source, MRS_TO_UPPER);
    if (!result) { sqlite3_result_error_nomem(context); return; }
    sqlite3_result_text(context, (const char*)result, -1, sqlite3_free);
}

// --- Реализация MRS_ILIKE (Использует хелпер lower) ---
static void mrs_ilike_native(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 2 || sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL) {
        sqlite3_result_null(context); return;
    }
    const char *pattern_raw = (const char*)sqlite3_value_text(argv[0]);
    const char *string_raw = (const char*)sqlite3_value_text(argv[1]);

    unsigned char *pattern_lower = mrs_utf8_case_alloc(pattern_raw, MRS_TO_LOWER);
    unsigned char *string_lower = mrs_utf8_case_alloc(string_raw, MRS_TO_LOWER);

    if (!pattern_lower || !string_lower) {
        if (pattern_lower) sqlite3_free(pattern_lower);
        if (string_lower) sqlite3_free(string_lower);
        sqlite3_result_error_nomem(context);
        return;
    }

    unsigned int escape_char = 0;
    if (argc == 3 && sqlite3_value_type(argv[2]) != SQLITE_NULL) {
        const char *escape_str = (const char*)sqlite3_value_text(argv[2]);
        if (escape_str && escape_str[0] != '\0') escape_char = (unsigned char)escape_str[0];
    }

    int match_status = sqlite3_strlike((const char*)pattern_lower, (const char*)string_lower, escape_char);

    sqlite3_free(pattern_lower);
    sqlite3_free(string_lower);

    sqlite3_result_int(context, match_status == 0 ? 1 : 0);
}

// --- Реализация REGEXP ---
static void mrs_regexp_native(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 2 || sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL) {
        sqlite3_result_int(context, 0);
        return;
    }

    const char *pattern_raw = (const char*)sqlite3_value_text(argv[0]);
    const char *value_raw = (const char*)sqlite3_value_text(argv[1]);

    if (!pattern_raw || !value_raw) {
        sqlite3_result_int(context, 0);
        return;
    }

    int cflags = REG_EXTENDED | REG_NOSUB;
    unsigned char *pattern_lower = NULL;
    unsigned char *value_lower = NULL;
    const char *normalized_pattern = pattern_raw;

    // Проверяем флаг регистронезависимости (?i)
    if (strncmp(pattern_raw, "(?i)", 4) == 0) {
        // Переводим шаблон (без префикса (?i)) и значение в нижний регистр
        pattern_lower = mrs_utf8_case_alloc(pattern_raw + 4, MRS_TO_LOWER);
        value_lower = mrs_utf8_case_alloc(value_raw, MRS_TO_LOWER);

        if (!pattern_lower || !value_lower) {
            if (pattern_lower) sqlite3_free(pattern_lower);
            if (value_lower) sqlite3_free(value_lower);
            sqlite3_result_error_nomem(context);
            return;
        }
        normalized_pattern = (const char*)pattern_lower;
    }

    regex_t reg;
    if (regcomp(&reg, normalized_pattern, cflags) != 0) {
        if (pattern_lower) sqlite3_free(pattern_lower);
        if (value_lower) sqlite3_free(value_lower);
        sqlite3_result_error(context, "Invalid regular expression pattern", -1);
        return;
    }

    // Если был флаг (?i), поиск идет по строкам в нижнем регистре
    const char *text_to_match = value_lower ? (const char*)value_lower : value_raw;
    int status = regexec(&reg, text_to_match, 0, NULL, 0);

    regfree(&reg);
    if (pattern_lower) sqlite3_free(pattern_lower);
    if (value_lower) sqlite3_free(value_lower);

    sqlite3_result_int(context, status == 0 ? 1 : 0);
}

#endif // MRS_TEXT_EXTENSIONS_HPP