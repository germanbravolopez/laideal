# Qt Test suites, each registered with CTest as its own executable.
# These link the production static libs and exercise them directly, so a
# regression in the covered modules fails `ctest` (and the release gate).
#
# Both use QTEST_GUILESS_MAIN (QCoreApplication) - none of the tests create a
# widget - so they run headless on CI without a display / platform plugin.

# sql_lite: integration tests against a throwaway SQLite DB (totals + Verifactu
# estado filters, accounting locks, invoice-seq, client lookup, hashing).
add_executable(test_sql_lite test_sql_lite.cpp)
target_link_libraries(test_sql_lite PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Sql
    Qt${QT_VERSION_MAJOR}::Widgets   # sql_lite references QMessageBox on error paths
    sql_lite
)
add_test(NAME sql_lite COMMAND test_sql_lite)

# tableview: diacritic-insensitive proxy-model filtering.
add_executable(test_mysortfilterproxymodel test_mysortfilterproxymodel.cpp)
target_link_libraries(test_mysortfilterproxymodel PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Widgets
    tableview
)
add_test(NAME mysortfilterproxymodel COMMAND test_mysortfilterproxymodel)
