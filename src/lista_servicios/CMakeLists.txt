add_library(lista_servicios STATIC
    lista_servicios.ui
    lista_servicios.h
    lista_servicios.cpp
)
target_link_libraries(lista_servicios
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    sql_lite
    TableView
)
target_include_directories(lista_servicios INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
