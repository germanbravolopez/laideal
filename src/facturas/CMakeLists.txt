add_library(facturas STATIC
    facturas.h
    facturas.cpp
)
target_link_libraries(facturas
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    sql_lite
    uikit
    verifactu_types
)
target_include_directories(facturas INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
