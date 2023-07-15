add_library(Listado STATIC
    listado.h
    listado.cpp
)
target_link_libraries(Listado
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    sql_lite
    TableView
    NumberFormatDelegate
)
target_include_directories(Listado INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
