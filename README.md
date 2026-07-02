![benchmark](benchmark2.jpg)

Created by [@ospfranco](https://twitter.com/ospfranco). **Please consider sponsoring!**.

OP-SQLite has grown large to cover a lot of plugins, sqlite versions and APIs. Please read the full documentation before opening an issue.

[Open the docs](https://op-engineering.github.io/op-sqlite/)

Join the Discord:

https://discord.gg/W9XmqCQCKP

Some of the big supported features:

- iOS, Android, macOS and web support
- Vanilla sqlite
- Turso is supported as a compilation target
- Libsql is supported as a compilation target
- SQLCipher is supported as a compilation target
- FTS5 plugin
- Rtree plugin
- cr-sqlite plugin
- sqlite-vec plugin
- Reactive queries
- Custom tokenizers
- Load runtime extensions
- JSONB support
- Native query interruption via `db.interrupt()`

It also contains a simple [Key-Value store](https://op-engineering.github.io/op-sqlite/docs/key_value_storage) you can use without adding one more dependency to your app.

# License

MIT License.


---

## 🛠️ MRS Fork Changes

This is a customized fork of `op-sqlite` that adds advanced Cyrillic (UTF-8) text processing, custom JSON path helpers, and native regex support.

* **Fixed Functions**: `lower()` and `upper()` now fully support Cyrillic characters.
* **New Operators**: Integrated native POSIX `REGEXP` operator support.
* **Custom Methods**: Added `mrs_ilike()` and `mrs_json_path()`.

👉 **For Developers**: If you want to contribute or add new C++ SQLite functions, please read our [Contributing Guide](./CONTRIBUTING.md).