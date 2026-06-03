set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS
    Core
    Gui
    Network
)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS
    Core
    Gui
    Network
)

set(PROJECT_SOURCES
    verifactuconfig.h
    verifactuconfig.cpp
    verifactutaxitem.h
    verifactutaxitem.cpp
    verifactuinvoice.h
    verifactuinvoice.cpp
    verifactutypes.h
    verifacturesponse.h
    verifacturesponse.cpp
    verifactumanager.h
    verifactumanager.cpp
    verifactuintegration.h
    verifactuintegration.cpp
)

add_library(verifactu ${PROJECT_SOURCES})

target_link_libraries(verifactu
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    appsettings
)

target_compile_definitions(verifactu PRIVATE
    PROJECT_VERSION="${PROJECT_VERSION}"
)

target_include_directories(verifactu
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
