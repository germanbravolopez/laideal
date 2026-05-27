# /coding-guidelines — Code Development Guidelines

Apply these guidelines to all new code written in this project. When modifying existing code, follow the guidelines for the new parts without renaming legacy identifiers (unnecessary renames break Qt auto-connect and add noise to diffs).

---

## Language rules — most important, no exceptions

| What | Language | Examples |
|------|----------|---------|
| Variables, methods, classes, enums | English | `totalPrice`, `saveTicket()`, `InvoiceType` |
| Comments | English | `// Calculate tax-inclusive total` |
| `qDebug` / `qWarning` / `qCritical` messages | English | `qDebug() << "Invoice submitted with CSV:" << csv` |
| User-facing UI strings | **Spanish** | `"No se ha introducido ningún cliente."` |
| Log messages vs UI messages | If the end user of the laundry app reads it at runtime → Spanish. Everything else → English. | |

User-facing means: text in `QMessageBox`, `QLabel`, window titles, `QAction` names, button text, status bar text, tooltips, and `QInputDialog` prompts.

---

## Naming conventions

| Construct | Style | Examples |
|-----------|-------|---------|
| Classes, structs | PascalCase | `MainWindow`, `VerifactuManager`, `VerifactuResult` |
| Methods and free functions | camelCase | `saveTicket()`, `validateForm()`, `generateQrCode()` |
| Private member variables | `m_` + camelCase | `m_verifactuIntegration`, `m_lastError`, `m_config` |
| Local variables | camelCase | `ticketNumber`, `totalPrice`, `invoiceDate` |
| Parameters | camelCase | `const QString &invoiceNumber` |
| Constants (`#define`, `const`) | `UPPER_SNAKE_CASE` | `DB_PATH`, `TABLE_TICKET_QNTY`, `C_TRIMESTRAL` |
| Qt auto-connect slots | `on_<objectName>_<signal>` | `on_pb_payment_toggled`, `on_le_search_returnPressed` |
| Files | lowercase, match class name | `verifactumanager.h`, `mainwindow.cpp` |
| Boolean variables / methods | `is`/`has`/`can` prefix | `isConfigured()`, `isPaid`, `hasError` |

> **Legacy note**: existing code in `src/app/`, `src/add_garment/`, and similar older modules uses `snake_case` for method names (e.g., `save_ticket()`, `reset_all_contents()`). Do **not** rename those — it breaks Qt auto-connect and adds unnecessary diff noise. Use camelCase for all **new** methods.

---

## UI widget naming (Qt Creator convention — keep consistent)

| Prefix | Widget type |
|--------|------------|
| `pb_` | `QPushButton` |
| `le_` | `QLineEdit` |
| `cb_` | `QComboBox` |
| `de_` | `QDateEdit` |
| `lbl_` | `QLabel` |
| `table_` | `QTableWidget` / `QTableView` |
| `bb_` | `QDialogButtonBox` |
| `menu` | `QMenu` |
| `action` | `QAction` |

---

## Qt-specific conventions

- **Memory management**: always pass a Qt parent (`this` or valid `QObject*`) to every `QObject`-derived object allocated with `new`. Do not use raw `delete` when Qt ownership covers it.
- **Signal/slot syntax**: prefer the new-style pointer syntax over macro syntax:
  ```cpp
  // Preferred
  connect(sender, &SenderClass::signal, receiver, &ReceiverClass::slot);
  // Avoid (unless Qt auto-connect requires it)
  connect(sender, SIGNAL(signal()), receiver, SLOT(slot()));
  ```
- **No Qt exceptions**: Qt does not use exceptions. Return `bool`, a result struct (see `VerifactuResult`), or a nullable `QString` for errors. Never throw.
- **Decimal separators**: always use `.` not `,` in code. The DB has a cleanup tool (`limpiar_base_de_datos`) precisely because `,` crept into data — do not introduce new occurrences.
- **Printing numbers**: use `QString::number(value, 'f', 2)` for currency. Never rely on locale-dependent formatting.

---

## Database rules

- Always open and close `db` within the same function scope:
  ```cpp
  db.open();
  QSqlQuery q;
  q.prepare("...");
  q.bindValue(":param", value);   // always use named params
  q.exec();
  db.close();
  ```
- **Never** build SQL by string concatenation — always use `:named_params` with `bindValue()`.
- No hardcoded paths. The existing `DB_PATH` in `sql_lite.h` is a known issue, not a pattern to follow.

---

## Error handling

- Return `bool` or a result struct from anything that can fail.
- Log failures in English with `qDebug()` (debug-level) or `qWarning()` (unexpected state).
- Show the user a `QMessageBox` in Spanish only when their input is wrong or they need to act.
- Never silently swallow errors — log even if you don't surface them to the user.

---

## Code structure and comments

- One class per `.h` / `.cpp` file pair.
- Header guard format: `#ifndef CLASSNAME_H` / `#define CLASSNAME_H` / `#endif // CLASSNAME_H`
- Include order: own header first, then Qt headers (`<QObject>`, etc.), then project headers (`"sql_lite.h"`).
- Comment only the **why**, not the **what** — good names make the what obvious.
- Use `// TODO:` for planned work, `// FIXME:` for known bugs. Do not commit code with `std::exit()` or `abort()` outside of main.
- Keep functions under ~40 lines. If a method grows beyond that, extract a named helper.

---

## What NOT to do

| Forbidden | Reason |
|-----------|--------|
| Hardcoded file paths | Breaks on any other machine — use config or relative paths |
| `std::exit()` / `abort()` in non-main code | Bypasses Qt cleanup; known issue in constructor — do not repeat |
| Spanish identifiers, variable names, or comments | Makes code harder to read for any non-Spanish developer or AI agent |
| English UI strings shown to the end user | The end user of the app is Spanish-speaking |
| SQL string concatenation | SQL injection risk and hard to debug |
| `using namespace std` or `using namespace Qt` | Causes ambiguity with Qt types |
| Raw `new` without Qt parent or paired `delete` | Memory leak |
| Committed debug-only code or `qDebug()` prints left in production paths | Use the `debug` flag in `MainWindow` or remove before merging |
| Comma as decimal separator in numeric strings | DB stores data with `.`; commas cause silent data corruption |
| Non-ASCII punctuation in `.cpp` / `.h` files: em-dash (`—` U+2014), en-dash (`–` U+2013), curly quotes (`“ ” ‘ ’`), ellipsis (`…` U+2026) | Some Windows code pages mangle these characters in source files; use plain ASCII (`-`, `"`, `'`, `...`) in comments, log strings, and string literals. Exception: Spanish characters (`á`, `é`, `í`, `ó`, `ú`, `ñ`, `¿`, `¡`, `°`) are required in user-facing UI strings and are allowed everywhere. |
