# Contabilidad (`src/contabilidad/`)

Generates HTML accounting reports and manages accounting period locks.

## Source files

- `src/contabilidad/contabilidad.h/cpp`

## Key interface

```cpp
Contabilidad *ui = new Contabilidad(this);
ui->db = db;
ui->revertirOn = false;  // set true to unlock instead of lock
ui->show();
```

`MainWindow` opens this dialog twice: once with `revertirOn=false` for normal accounting generation, and once with `revertirOn=true` for the "revert accounting" action.

## Report modes

| Constant | Mode | Description |
|----------|------|-------------|
| `C_MENSUAL` | Monthly | Report for a single month |
| `C_TRIMESTRAL` | Quarterly | Report for a quarter (Q1–Q4) |
| `C_ANUAL` | Annual | Full year report |

## On accept

1. `generateContabilidad()` builds an HTML report: income (`ingresos`) and expenses (`gastos`) tables, with IVA breakdown per rate.
2. `updateLock()` sets `edit_lock` on all affected `ingresos` rows:
   - `revertirOn=false` → sets `edit_lock=1` (locks the period)
   - `revertirOn=true` → sets `edit_lock=0` (unlocks the period)

## Output

The HTML report is written to a hardcoded path (`C:/Users/rocio/OneDrive/Desktop/Tintoreria/...`) and opened automatically via `QDesktopServices::openUrl()`.

Known issue: output path is hardcoded (same issue as DB path).

## IVA breakdown

`getTotalIncome(table, iva, trimForYearConfig)` sums `importe` for a specific IVA rate and period. Called separately for 21%, 10%, and 0% rates when generating the report.
