add_library(lista_clientes STATIC
    lista_clientes.ui
    lista_clientes.h
    lista_clientes.cpp
)
target_link_libraries(lista_clientes
    PRIVATE Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    sql_lite
    TableView
)
target_include_directories(lista_clientes INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
