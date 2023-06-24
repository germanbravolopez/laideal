add_library(gastos STATIC
    mysortfilterproxymodel.h
    mysortfilterproxymodel.cpp
    generar_listado.ui
    generar_listado.h
    generar_listado.cpp
    gastos.ui
    gastos.h
    gastos.cpp
)
target_link_libraries(gastos
    PRIVATE Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    sql_lite
    Qt${QT_VERSION_MAJOR}::PrintSupport
)
target_include_directories(gastos INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
