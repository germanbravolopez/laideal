add_library(NumberFormatDelegate STATIC
    numberformatdelegate.h
    numberformatdelegate.cpp
)
target_link_libraries(NumberFormatDelegate
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
)
target_include_directories(NumberFormatDelegate INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
