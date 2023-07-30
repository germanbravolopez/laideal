add_library(Listado STATIC
    genlistado.ui
    genlistado.h
    genlistado.cpp
    insertnewitem.h
    insertnewitem.cpp
    listado.h
    listado.cpp
)
target_link_libraries(Listado
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    Qt${QT_VERSION_MAJOR}::PrintSupport
    sql_lite
    TableView
    NumberFormatDelegate
    FilterWidget
    MySortFilterProxyModel
)
target_include_directories(Listado INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
