add_library(facturas STATIC
    facturas.ui
    facturas.h
    facturas.cpp
)
target_link_libraries(facturas
    PRIVATE Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    sql_lite
)
target_include_directories(contabilidad INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
