add_library(contabilidad STATIC
    contabilidad.ui
    contabilidad.h
    contabilidad.cpp
)
target_link_libraries(contabilidad
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    sql_lite
    appsettings
    Qt${QT_VERSION_MAJOR}::PrintSupport
)
target_include_directories(contabilidad INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
