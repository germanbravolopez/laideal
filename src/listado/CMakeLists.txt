add_library(listado STATIC
    genlistado.h
    genlistado.cpp
    insertnewitem.h
    insertnewitem.cpp
    listado.h
    listado.cpp
)
target_link_libraries(listado
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    sql_lite
    appsettings
    reporthtml
    uikit
    PUBLIC
    tableview
)
target_include_directories(listado INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
