add_library(MySortFilterProxyModel STATIC
    mysortfilterproxymodel.h
    mysortfilterproxymodel.cpp
)
target_link_libraries(MySortFilterProxyModel
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
)
target_include_directories(MySortFilterProxyModel INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
