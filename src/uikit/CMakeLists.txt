# Shared dialog style (see uikit.h): every window built in code takes its panels,
# buttons and result messages from here.
add_library(uikit STATIC
    uikit.h
    uikit.cpp
)
target_link_libraries(uikit
    PUBLIC
    Qt${QT_VERSION_MAJOR}::Widgets
)
target_include_directories(uikit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
