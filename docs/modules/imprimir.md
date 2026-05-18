# Imprimir (`src/imprimir/`)

Creates Excel files for printing receipts and formal invoices, then launches an external process to print them.

## Source files

- `src/imprimir/imprimir.h/cpp`

## Key interface

```cpp
Imprimir *ui = new Imprimir(this);
ui->db = db;
ui->isRecibo = true;            // receipt layout
ui->isCompleteInvoice = false;  // true adds extra fields (policy, stamp, etc.)
ui->getTicketInfo();            // shows dialog for ticket number; fetches rows from DB
ui->createTicketExcel(copyForClient, addPayedInfo);
ui->printTicket();
```

## Layout modes

| `isRecibo` | `isCompleteInvoice` | Layout |
|------------|---------------------|--------|
| `true` | `false` | Standard customer receipt |
| `false` | `false` | Simple invoice |
| `false` | `true` | Full invoice with extra info dialog prompts |

## Process

1. User enters a ticket number in the dialog shown by `getTicketInfo()`.
2. All matching rows from `ingresos` are loaded into `sqlQueryModel`.
3. `createTicketExcel()` builds an `.xlsx` file via `QXlsx` with appropriate column widths, styles, and row content.
4. `printTicket()` launches an external `.bat` script that sends the Excel to the printer.

## Table column indices (imprimir.h)

`TABLE_TICKET=0`, `TABLE_CLIENT=1`, `TABLE_DATE_RCP=2`, `TABLE_DATE_PAY=3`, `TABLE_DATE_PKU=4`, `TABLE_PRICE=5`, `TABLE_IS_PAYED=6`, `TABLE_STATE=7`, `TABLE_QUANTITY=8`, `TABLE_GARMENT=9`, `TABLE_SIZE=10`, `TABLE_SERVICE=11`, `TABLE_OBSERV=12`, `TABLE_EDIT_LOCK=13`, `TABLE_HASH=14`

## Dependencies

- `QXlsx` — third-party Excel r/w library in `QXlsx/` (do not modify)
- External `.bat` printing script — path set at deploy time
