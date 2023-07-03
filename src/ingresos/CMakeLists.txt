add_library(ingresos STATIC
    ingresos.ui
    ingresos.h
    ingresos.cpp
)
target_link_libraries(ingresos
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    NumberFormatDelegate
)
target_include_directories(ingresos INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
