add_library(lista_prendas STATIC
    anadirprenda.ui
    anadirprenda.h
    anadirprenda.cpp
    lista_prendas.ui
    lista_prendas.h
    lista_prendas.cpp
)
target_link_libraries(lista_prendas
    PRIVATE Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    sql_lite
)
target_include_directories(lista_prendas INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
