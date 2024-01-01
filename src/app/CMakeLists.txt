add_executable(laideal
    mainwindow.ui
    mainwindow.h
    mainwindow.cpp
    main.cpp
)

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
)

set_target_properties(laideal PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)
