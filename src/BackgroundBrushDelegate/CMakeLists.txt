add_library(BackgroundBrushDelegate STATIC
    backgroundbrushdelegate.h
    backgroundbrushdelegate.cpp
)
target_link_libraries(BackgroundBrushDelegate
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
)
target_include_directories(BackgroundBrushDelegate INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
