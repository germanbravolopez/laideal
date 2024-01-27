configure_file(version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

if (WIN32)
    configure_file(
        ${CMAKE_SOURCE_DIR}/resources/laideal.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/laideal.rc @ONLY
    )

    set(EXE_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/laideal.rc)
endif()

add_executable(laideal
    mainwindow.ui
    mainwindow.h
    mainwindow.cpp
    main.cpp
    ${EXE_SOURCES}
)

target_include_directories(laideal PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(laideal
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    sql_lite
    imprimir
    Listado
    recog_prendas
    sql_lite
    MySortFilterProxyModel
    FilterWidget
    TableView
    contabilidad
    facturas
    add_garment
)

set_target_properties(laideal PROPERTIES
    VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)
