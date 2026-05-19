add_library(listado STATIC
    genlistado.ui
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
    Qt${QT_VERSION_MAJOR}::PrintSupport
    sql_lite
    appsettings
    PUBLIC
    tableview
)
target_include_directories(listado INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
