add_library(lista_proveedores STATIC
    lista_proveedores.ui
    lista_proveedores.h
    lista_proveedores.cpp
)
target_link_libraries(lista_proveedores
    PRIVATE Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    sql_lite
    TableView
)
target_include_directories(lista_proveedores INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
