add_library(add_garment STATIC
    add_garment.ui
    add_garment.h
    add_garment.cpp
)
target_link_libraries(add_garment
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    sql_lite
    imprimir
)
target_include_directories(add_garment INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
