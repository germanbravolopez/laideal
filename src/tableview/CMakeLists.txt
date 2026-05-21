add_library(tableview STATIC
    tableview.h
    tableview.cpp
    filterwidget.h
    filterwidget.cpp
    mysortfilterproxymodel.h
    mysortfilterproxymodel.cpp
    numberformatdelegate.h
    numberformatdelegate.cpp
    textcolordelegate.h
    textcolordelegate.cpp
    linkdelegate.h
    linkdelegate.cpp
)
target_link_libraries(tableview
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
)
target_include_directories(tableview INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
