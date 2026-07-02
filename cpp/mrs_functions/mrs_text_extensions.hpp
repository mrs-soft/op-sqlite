#ifndef MRS_TEXT_EXTENSIONS_HPP
#define MRS_TEXT_EXTENSIONS_HPP

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <regex>
#include <locale>
#include "../sqlite3.h"

typedef enum { MRS_TO_LOWER, MRS_TO_UPPER } MrsCaseMode;

// Быстрый инлайн-хелпер для приведения ОДНОГО символа UTF-16 к нижнему регистру
static inline unsigned short mrs_utf16_tolower_char(unsigned short ch) {
    if (ch >= 0x0410 && ch <= 0x042F) return ch + 0x20; // А-Я -> а-я
    if (ch == 0x0401) return 0x0451;                   // Ё -> ё
    if (ch >= 'A' && ch <= 'Z') return ch + ('a' - 'A'); // A-Z -> a-z
    return ch;
}

// --- ЕДИНЫЙ ХЕЛПЕР для LOWER и UPPER ---
static unsigned short* mrs_utf16_case_alloc(const unsigned short *source, size_t len, MrsCaseMode mode) {
    if (!source) return NULL;

    unsigned short *result = (unsigned short*)sqlite3_malloc64((len + 1) * 2);
    if (result == NULL) return NULL;

    for (size_t i = 0; i < len; i++) {
        unsigned short ch = source[i];
        if (mode == MRS_TO_LOWER) {
            result[i] = mrs_utf16_tolower_char(ch);
        } else {
            if (ch >= 0x0430 && ch <= 0x044F) result[i] = ch - 0x20;
            else if (ch == 0x0451) result[i] = 0x0401;
            else if (ch >= 'a' && ch <= 'z') result[i] = ch - ('a' - 'A');
            else result[i] = ch;
        }
    }
    result[len] = 0;
    return result;
}

// --- Реализация LOWER ---
static void mrs_lower_native(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 1 || sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_null(context);
        return;
    }
    const unsigned short *source = (const unsigned short*)sqlite3_value_text16(argv[0]);
    if (!source) return;
    size_t len = sqlite3_value_bytes16(argv[0]) / 2;
    unsigned short *result = mrs_utf16_case_alloc(source, len, MRS_TO_LOWER);
    if (!result) {
        sqlite3_result_error_nomem(context);
        return;
    }
    sqlite3_result_text16(context, (const char*)result, -1, sqlite3_free);
}

// --- Реализация UPPER ---
static void mrs_upper_native(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 1 || sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_null(context);
        return;
    }
    const unsigned short *source = (const unsigned short*)sqlite3_value_text16(argv[0]);
    if (!source) return;
    size_t len = sqlite3_value_bytes16(argv[0]) / 2;
    unsigned short *result = mrs_utf16_case_alloc(source, len, MRS_TO_UPPER);
    if (!result) {
        sqlite3_result_error_nomem(context);
        return;
    }
    sqlite3_result_text16(context, (const char*)result, -1, sqlite3_free);
}

// --- Правильный, безопасный алгоритм UTF-16 LIKE ---
static int mrs_utf16_like(const unsigned short *pat, const unsigned short *str, unsigned short escape) {
    if (!pat || !str) return 0;

    while (*pat) {
        // 1. Обработка escape-символа
        if (escape && *pat == escape) {
            pat++; // Пропускаем сам escape-символ
            if (!*pat) return 0; // Исключение: escape на конце строки

            // Сравниваем следующий символ без учета регистра
            if (mrs_utf16_tolower_char(*pat) != mrs_utf16_tolower_char(*str)) {
                return 0;
            }
            pat++; str++;
            continue;
        }

        // 2. Обработка подстановочного знака '%'
        if (*pat == '%') {
            while (*pat == '%') pat++; // Схлопываем идущие подряд '%%%'
            if (!*pat) return 1;       // Если '%' был последним — это 100% совпадение

            // Рекурсивно ищем совпадение оставшейся части паттерна в строке str
            while (*str) {
                if (mrs_utf16_like(pat, str, escape)) return 1;
                str++;
            }
            return 0;
        }

        // 3. Обработка подстановочного знака '_'
        if (*pat == '_') {
            if (!*str) return 0; // Строка закончилась раньше времени
            pat++; str++;
            continue;
        }

        // 4. Обычное посимвольное сравнение регистронезависимо
        if (mrs_utf16_tolower_char(*pat) != mrs_utf16_tolower_char(*str)) {
            return 0;
        }
        pat++; str++;
    }

    return *str == 0;
}

// --- Реализация MRS_ILIKE ---
static void mrs_ilike_native(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc < 2 || sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL) {
        sqlite3_result_null(context);
        return;
    }
    const unsigned short *pattern = (const unsigned short*)sqlite3_value_text16(argv[0]);
    const unsigned short *string = (const unsigned short*)sqlite3_value_text16(argv[1]);
    if (!pattern || !string) return;

    unsigned short escape_char = 0;
    if (argc == 3 && sqlite3_value_type(argv[2]) != SQLITE_NULL) {
        const unsigned short *escape_str = (const unsigned short*)sqlite3_value_text16(argv[2]);
        if (escape_str && escape_str[0] != 0) {
            escape_char = escape_str[0];
        }
    }

    int match = mrs_utf16_like(pattern, string, escape_char);
    sqlite3_result_int(context, match);
}

// --- Нативная реализация REGEXP (POSIX) ---
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

    std::string pattern(pattern_raw);
    std::string value(value_raw);

    // Настройка флагов компиляции регулярного выражения
    std::regex_constants::syntax_option_type flags = std::regex_constants::ECMAScript;

    // Обработка вашего кастомного префикса (?i) для игнорирования регистра
    if (pattern.rfind("(?i)", 0) == 0) { // Проверяем, начинается ли строка с (?i)
        flags |= std::regex_constants::icase;
        pattern = pattern.substr(4); // Отрезаем префикс
    }

    try {
        // Создаем регулярное выражение с принудительной локалью UTF-8 для корректной работы кириллицы
        std::regex re;
        re.imbue(std::locale("en_US.UTF-8")); // На Android NDK этого достаточно для работы UTF-8
        re.assign(pattern, flags);

        // Ищем совпадение в строке (std::regex_search ищет подстроку, аналог поведения LIKE/REGEXP)
        bool match = std::regex_search(value, re);

        sqlite3_result_int(context, match ? 1 : 0);
    }
    catch (const std::regex_error& e) {
        // Если пользователь ввел кривой паттерн, база не упадет, а выдаст ошибку
        sqlite3_result_error(context, "Syntax error in regular expression", -1);
    }
}

#endif // MRS_TEXT_EXTENSIONS_HPP