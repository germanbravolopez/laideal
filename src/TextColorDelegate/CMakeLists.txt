add_library(TextColorDelegate STATIC
    textcolordelegate.h
    textcolordelegate.cpp
)
target_link_libraries(TextColorDelegate
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
)
target_include_directories(TextColorDelegate INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
