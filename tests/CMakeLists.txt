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
add_executable(test_sql_lite test_sql_lite.cpp support/testschema.h)
target_link_libraries(test_sql_lite PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Sql
    Qt${QT_VERSION_MAJOR}::Widgets   # sql_lite references QMessageBox on error paths
    sql_lite
)
# verifactutypes.h is header-only (VerifactuResult + the estado helpers), so the
# include path is enough - no need to link the whole verifactu lib and its Network dep.
target_include_directories(test_sql_lite PRIVATE ${CMAKE_SOURCE_DIR}/src/verifactu)
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

# tableview: TextColorDelegate::classify pure colour decision (incl. the issue
# #40 rule that a voided "Anulado" row renders green).
add_executable(test_textcolordelegate test_textcolordelegate.cpp)
target_link_libraries(test_textcolordelegate PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Widgets
    tableview
)
# ingresos_schema.h (column indices) is header-only: the include path is enough.
target_include_directories(test_textcolordelegate PRIVATE ${CMAKE_SOURCE_DIR}/src/sql_lite)
add_test(NAME textcolordelegate
    COMMAND test_textcolordelegate -o ${CMAKE_BINARY_DIR}/test-results-textcolordelegate.xml,junitxml -o -,txt)
# QTEST_MAIN (QApplication) for the header-placement check: run it headless.
set_tests_properties(textcolordelegate PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
# The release-notes parity test reads both files from the source tree.
target_compile_definitions(test_appsettings PRIVATE LAIDEAL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME appsettings
    COMMAND test_appsettings -o ${CMAKE_BINARY_DIR}/test-results-appsettings.xml,junitxml -o -,txt)

# settingsdialog: Verifactu service-key show/hide toggle, driven on the real
# dialog. QTEST_MAIN needs a QApplication, so it runs under the offscreen QPA.
add_executable(test_settingsdialog test_settingsdialog.cpp)
target_link_libraries(test_settingsdialog PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Widgets
    appsettings
)
add_test(NAME settingsdialog
    COMMAND test_settingsdialog -o ${CMAKE_BINARY_DIR}/test-results-settingsdialog.xml,junitxml -o -,txt)
set_tests_properties(settingsdialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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

# AeatHash: record hash (huella) and QR URL of a direct AEAT client, checked against
# the official AEAT examples. Pure.
add_executable(test_aeat_hash test_aeat_hash.cpp)
target_link_libraries(test_aeat_hash PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    verifactu
)
add_test(NAME aeat_hash
    COMMAND test_aeat_hash -o ${CMAKE_BINARY_DIR}/test-results-aeat_hash.xml,junitxml -o -,txt)

# AeatRecord: the direct client's records and SOAP envelopes. It also writes sample
# envelopes into build/aeat-xml, which aeat_xsd validates against the official AEAT
# schemas (tests/fixtures/aeat-xsd, unchanged) with Python + lxml - skipped when
# lxml is missing locally; CI installs it.
add_executable(test_aeat_record test_aeat_record.cpp)
target_link_libraries(test_aeat_record PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    verifactu
)
add_test(NAME aeat_record
    COMMAND test_aeat_record -o ${CMAKE_BINARY_DIR}/test-results-aeat_record.xml,junitxml -o -,txt)
set_tests_properties(aeat_record PROPERTIES
    ENVIRONMENT "AEAT_XML_OUT=${CMAKE_BINARY_DIR}/aeat-xml"
    FIXTURES_SETUP aeat_xml)
# AeatResponse: AEAT's replies (fixtures in tests/fixtures/aeat-responses, also
# schema-checked by aeat_xsd) and their mapping onto the app's result types.
add_executable(test_aeat_response test_aeat_response.cpp)
target_link_libraries(test_aeat_response PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    verifactu
)
target_compile_definitions(test_aeat_response PRIVATE
    AEAT_RESPONSES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/aeat-responses")
add_test(NAME aeat_response
    COMMAND test_aeat_response -o ${CMAKE_BINARY_DIR}/test-results-aeat_response.xml,junitxml -o -,txt)

# AeatStore: the direct client's chain head and record outbox (throwaway SQLite DB).
add_executable(test_aeat_store test_aeat_store.cpp)
target_link_libraries(test_aeat_store PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Sql
    verifactu
)
add_test(NAME aeat_store
    COMMAND test_aeat_store -o ${CMAKE_BINARY_DIR}/test-results-aeat_store.xml,junitxml -o -,txt)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME aeat_xsd
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/aeat/validate_aeat_xml.py
                ${CMAKE_SOURCE_DIR}/tests/fixtures/aeat-xsd ${CMAKE_BINARY_DIR}/aeat-xml
                ${CMAKE_SOURCE_DIR}/tests/fixtures/aeat-responses)
    set_tests_properties(aeat_xsd PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_REQUIRED aeat_xml)
endif()

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

# printing: pure ESC/POS builder (exact bytes + PC858 transcode + column math)
# and the TicketRenderer receipt/invoice layout. QTEST_GUILESS_MAIN - QImage
# works under QCoreApplication, no platform plugin needed.
add_executable(test_escpos test_escpos.cpp)
target_link_libraries(test_escpos PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Gui       # QImage (raster path + TicketData::qr)
    printing
)
add_test(NAME escpos
    COMMAND test_escpos -o ${CMAKE_BINARY_DIR}/test-results-escpos.xml,junitxml -o -,txt)

# ticket_preview: renders a sample recibo + factura from the real TicketRenderer
# output back into a PNG simulation of the thermal paper + an ASCII mock (written
# next to the test exe). QTEST_MAIN (QGuiApplication - links Gui, not Widgets)
# under offscreen so QPainter text rasterizes headless.
add_executable(test_ticket_preview test_ticket_preview.cpp)
target_link_libraries(test_ticket_preview PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Gui
    printing
)
add_test(NAME ticket_preview
    COMMAND test_ticket_preview -o ${CMAKE_BINARY_DIR}/test-results-ticket_preview.xml,junitxml -o -,txt)
# On Windows use the native QPA platform so system fonts render real glyphs in
# the preview PNG (the offscreen plugin draws tofu boxes there); painting to a
# QImage needs no visible window. Off Windows (Linux CI) force offscreen so the
# GUI test runs headless - the asserts are ASCII-content based, not pixels.
if(NOT WIN32)
    set_tests_properties(ticket_preview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# contabilidad: pure period-range date math (Contabilidad::periodRangeFor).
add_executable(test_contabilidad test_contabilidad.cpp)
target_link_libraries(test_contabilidad PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql      # contabilidad.h includes <QSqlQueryModel>
    contabilidad
    reporthtml                      # formatEuro, to assert the detail-table amounts
)
add_test(NAME contabilidad
    COMMAND test_contabilidad -o ${CMAKE_BINARY_DIR}/test-results-contabilidad.xml,junitxml -o -,txt)

# End-to-end test bench (phase 1): PayDialog + the real Verifactu client and HTTP
# stack + sql_lite, against a seeded throwaway DB and a local fake Verifactu server
# (tests/support), never the real AEAT. QTEST_MAIN under offscreen.
add_executable(test_e2e_verifactu
    test_e2e_verifactu.cpp
    support/fakeverifactuserver.h
    support/fakeverifactuserver.cpp
    support/modalautocloser.h
    support/testschema.h
)
target_include_directories(test_e2e_verifactu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_SOURCE_DIR}/src/verifactu
    ${CMAKE_SOURCE_DIR}/src/recog_prendas
)
target_link_libraries(test_e2e_verifactu PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    Qt${QT_VERSION_MAJOR}::Network
    recog_prendas
    verifactu
    sql_lite
    appsettings
    imprimir
    contabilidad
)
add_test(NAME e2e_verifactu
    COMMAND test_e2e_verifactu -o ${CMAKE_BINARY_DIR}/test-results-e2e_verifactu.xml,junitxml -o -,txt)
set_tests_properties(e2e_verifactu PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Test bench phase 2: the application's own windows (MainWindow and the app
# dialogs, from the laideal_app library) driven at screen level against the same
# fake Verifactu server and throwaway DB / settings.
add_executable(test_e2e_app
    test_e2e_app.cpp
    support/e2efixture.h
    support/fakeverifactuserver.h
    support/fakeverifactuserver.cpp
    support/modalautocloser.h
    support/modaldriver.h
    support/testschema.h
)
target_include_directories(test_e2e_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/support
)
target_link_libraries(test_e2e_app PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Network
    laideal_app
    reporthtml
)
add_test(NAME e2e_app
    COMMAND test_e2e_app -o ${CMAKE_BINARY_DIR}/test-results-e2e_app.xml,junitxml -o -,txt)
set_tests_properties(e2e_app PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Fail any suite that hangs (e.g. a blocking system call or an unanswered modal
# dialog under the offscreen platform) instead of stalling CI until the job
# limit; ctest then prints the suite's output up to the hang.
get_property(_laideal_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${_laideal_tests} PROPERTIES TIMEOUT 120)
