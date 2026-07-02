#ifndef MRS_SQLITE_FUNCTIONS_HPP
#define MRS_SQLITE_FUNCTIONS_HPP

#include "sqlite3.h"

// Подключаем наши изолированные модули из подпапки
#include "mrs_functions/mrs_text_extensions.hpp"
#include "mrs_functions/mrs_json_extensions.hpp"

#ifdef OP_SQLITE_USE_LIBSQL
#include "libsql/bridge.hpp"
#endif

// Единая функция для обычного SQLite
static void mrs_register_all_functions(sqlite3 *db) {
    if (db == NULL) return;

 // Переопределяем стандартные методы SQLite (теперь они знают про кириллицу)
    sqlite3_create_function(db, "lower", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, 0, mrs_lower_native, 0, 0);
    sqlite3_create_function(db, "upper", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, 0, mrs_upper_native, 0, 0);

    // Регистрируем кастомные методы
    sqlite3_create_function(db, "mrs_ilike", -1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, 0, mrs_ilike_native, 0, 0);
    sqlite3_create_function(db, "regexp", 2, SQLITE_UTF8 | SQLITE_DETERMINISTIC, 0, mrs_regexp_native, 0, 0);
    sqlite3_create_function(db, "mrs_json_path", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, 0, mrs_json_path_native, 0, 0);
}

// Перегрузка для LibSQL (чтобы не ломалась сборка бэкендов)
#ifdef OP_SQLITE_USE_LIBSQL
static void mrs_register_all_functions(opsqlite::DB libsql_db) {
    mrs_register_all_functions(libsql_db.db);
}
#endif

#endif // MRS_SQLITE_FUNCTIONS_HPP