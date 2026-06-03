# Qt Test suites, each registered with CTest as its own executable.
# These link the production static libs and exercise them directly, so a
# regression in the covered modules fails `ctest` (and the release gate).
#
# Both use QTEST_GUILESS_MAIN (QCoreApplication) - none of the tests create a
# widget - so they run headless on CI without a display / platform plugin.

# sql_lite: integration tests against a throwaway SQLite DB (totals + Verifactu
# estado filters, accounting locks, invoice-seq, client lookup, hashing).
# Each test runs with two -o sinks: a per-suite Qt JUnit XML in the build dir
# (one <testcase> per test method - consumed by the CI step that renders the
# foldable per-method summary) and the plain-text report on stdout (so ctest
# --output-on-failure still shows readable output on failure).
add_executable(test_sql_lite test_sql_lite.cpp)
target_link_libraries(test_sql_lite PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Sql
    Qt${QT_VERSION_MAJOR}::Widgets   # sql_lite references QMessageBox on error paths
    sql_lite
)
add_test(NAME sql_lite
    COMMAND test_sql_lite -o ${CMAKE_BINARY_DIR}/test-results-sql_lite.xml,junitxml -o -,txt)

# tableview: diacritic-insensitive proxy-model filtering.
add_executable(test_mysortfilterproxymodel test_mysortfilterproxymodel.cpp)
target_link_libraries(test_mysortfilterproxymodel PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Widgets
    tableview
)
add_test(NAME mysortfilterproxymodel
    COMMAND test_mysortfilterproxymodel -o ${CMAKE_BINARY_DIR}/test-results-mysortfilterproxymodel.xml,junitxml -o -,txt)

# listado: GenListado pure helpers (filename suffix + row filter).
add_executable(test_genlistado test_genlistado.cpp)
target_link_libraries(test_genlistado PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Widgets   # genlistado.h includes <QDialog>
    Qt${QT_VERSION_MAJOR}::Sql       # genlistado.h includes <QSqlDatabase>
    listado
)
add_test(NAME genlistado
    COMMAND test_genlistado -o ${CMAKE_BINARY_DIR}/test-results-genlistado.xml,junitxml -o -,txt)

# facturas: pure IVA split (base + cuota from a gross amount).
add_executable(test_facturas test_facturas.cpp)
target_link_libraries(test_facturas PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Widgets   # facturas.h includes <QMainWindow>
    Qt${QT_VERSION_MAJOR}::Sql       # facturas.h includes <QSqlQueryModel>
    facturas
)
add_test(NAME facturas
    COMMAND test_facturas -o ${CMAKE_BINARY_DIR}/test-results-facturas.xml,junitxml -o -,txt)

# appsettings: DPAPI secret-at-rest helpers (encrypt/decrypt round-trip + marker).
add_executable(test_appsettings test_appsettings.cpp)
target_link_libraries(test_appsettings PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    appsettings
)
add_test(NAME appsettings
    COMMAND test_appsettings -o ${CMAKE_BINARY_DIR}/test-results-appsettings.xml,junitxml -o -,txt)

# reporthtml: pure currency / scaffolding helpers.
add_executable(test_reporthtml test_reporthtml.cpp)
target_link_libraries(test_reporthtml PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    reporthtml
)
add_test(NAME reporthtml
    COMMAND test_reporthtml -o ${CMAKE_BINARY_DIR}/test-results-reporthtml.xml,junitxml -o -,txt)

# updater: pure version-compare helpers. NB the executable must NOT be named
# *update*/*setup*/*install* - Windows' UAC installer-detection heuristic would
# flag such a name as requiring elevation and refuse to launch it under CTest.
add_executable(test_versioncompare test_updater.cpp)
target_link_libraries(test_versioncompare PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    updater
)
add_test(NAME version_compare
    COMMAND test_versioncompare -o ${CMAKE_BINARY_DIR}/test-results-versioncompare.xml,junitxml -o -,txt)

# verifactu: the pure JSON response parser (parseVerifactuResponse). Uses
# QTEST_MAIN (QApplication for QPixmap) under the offscreen platform so the QR
# decode path runs headless.
add_executable(test_verifactu_response test_verifactu_response.cpp)
target_link_libraries(test_verifactu_response PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    verifactu
)
add_test(NAME verifactu_response
    COMMAND test_verifactu_response -o ${CMAKE_BINARY_DIR}/test-results-verifactu_response.xml,junitxml -o -,txt)
set_tests_properties(verifactu_response PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# verifactu model classes (config / tax item / invoice / estado), all pure.
add_executable(test_verifactu_models test_verifactu_models.cpp)
target_link_libraries(test_verifactu_models PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    verifactu
)
add_test(NAME verifactu_models
    COMMAND test_verifactu_models -o ${CMAKE_BINARY_DIR}/test-results-verifactu_models.xml,junitxml -o -,txt)

# backup: pure retention/pruning decision.
add_executable(test_backup_manager test_backup_manager.cpp)
target_link_libraries(test_backup_manager PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Sql
    backup
)
add_test(NAME backup_manager
    COMMAND test_backup_manager -o ${CMAKE_BINARY_DIR}/test-results-backup_manager.xml,junitxml -o -,txt)

# contabilidad: pure period-range date math (Contabilidad::periodRangeFor).
add_executable(test_contabilidad test_contabilidad.cpp)
target_link_libraries(test_contabilidad PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql      # contabilidad.h includes <QSqlQueryModel>
    contabilidad
)
add_test(NAME contabilidad
    COMMAND test_contabilidad -o ${CMAKE_BINARY_DIR}/test-results-contabilidad.xml,junitxml -o -,txt)
