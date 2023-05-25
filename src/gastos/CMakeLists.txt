add_library(gastos STATIC
    mysortfilterproxymodel.h
    mysortfilterproxymodel.cpp
    gastos.ui
    gastos.h
    gastos.cpp
)
target_link_libraries(gastos PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Sql)
target_include_directories(gastos INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
