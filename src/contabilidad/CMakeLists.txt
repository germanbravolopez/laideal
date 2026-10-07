add_library(contabilidad STATIC
    contabilidad.ui
    contabilidad.h
    contabilidad.cpp
)
target_link_libraries(contabilidad
    PUBLIC
    sql_lite                        # contabilidad.h exposes the detail row types
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    appsettings
    reporthtml
    Qt${QT_VERSION_MAJOR}::PrintSupport
)
target_include_directories(contabilidad INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
