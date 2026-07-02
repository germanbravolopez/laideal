set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
)

add_library(updater STATIC
    updater.h
    updater.cpp
    updaterdialog.h
    updaterdialog.cpp
)

target_link_libraries(updater
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
)

# Inherits PROJECT_VERSION from the top-level project() call.
target_compile_definitions(updater PRIVATE
    PROJECT_VERSION="${PROJECT_VERSION}"
)

target_include_directories(updater
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
