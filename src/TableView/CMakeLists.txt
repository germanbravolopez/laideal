add_library(TableView STATIC
    tableview.h
    tableview.cpp
)
target_link_libraries(TableView PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
target_include_directories(TableView INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
