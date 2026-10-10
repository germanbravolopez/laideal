configure_file(version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

if (WIN32)
    string(TIMESTAMP CURRENT_YEAR "%Y")
    configure_file(
        ${CMAKE_SOURCE_DIR}/resources/laideal.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/laideal.rc @ONLY
    )

    set(EXE_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/laideal.rc)
endif()

# The application's own windows (MainWindow + the Verifactu / void / pending
# dialogs) live in a static library so the end-to-end test bench can link and
# drive them at screen level (tests/test_e2e_app.cpp). The executable is only
# main.cpp plus the Qt resources (icon, release notes, Qt translations) and the
# Windows version resource.
add_library(laideal_app STATIC
    mainwindow.ui
    mainwindow.h
    mainwindow.cpp
    cancelinvoicedialog.h
    cancelinvoicedialog.cpp
    rectifyinvoicedialog.h
    rectifyinvoicedialog.cpp
    pendingsubmitsdialog.h
    pendingsubmitsdialog.cpp
    aeatexport.h
    aeatexport.cpp
    aeatexportdialog.h
    aeatexportdialog.cpp
)

target_include_directories(laideal_app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}          # version.h
)

target_link_libraries(laideal_app
    PUBLIC
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    logging
    appsettings
    sql_lite
    imprimir
    listado
    recog_prendas
    tableview
    contabilidad
    facturas
    add_garment
    verifactu
    updater
    backup
    uikit
)

add_executable(laideal
    main.cpp
    ${CMAKE_SOURCE_DIR}/resources/laideal.qrc
    ${EXE_SOURCES}
)

target_link_libraries(laideal PRIVATE laideal_app)

set_target_properties(laideal PROPERTIES
    VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)
