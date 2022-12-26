add_library(imprimir STATIC
    imprimir.ui
    imprimir.h
    imprimir.cpp
)
target_link_libraries(imprimir PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    sql_lite
    Qt${QT_VERSION_MAJOR}::PrintSupport
)
target_include_directories(imprimir INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
