add_library(FilterWidget STATIC
    filterwidget.h
    filterwidget.cpp
)
target_link_libraries(FilterWidget
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
)
target_include_directories(FilterWidget INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
