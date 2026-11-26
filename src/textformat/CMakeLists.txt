add_library(textformat STATIC
    textformat.h
    textformat.cpp
)
target_link_libraries(textformat
    PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
)
target_include_directories(textformat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
