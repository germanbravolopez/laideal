add_library(logging STATIC
    applogger.h
    applogger.cpp
)
target_link_libraries(logging PUBLIC Qt${QT_VERSION_MAJOR}::Core)
target_include_directories(logging INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
