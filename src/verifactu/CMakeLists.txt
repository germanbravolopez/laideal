set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS
    Core
    Gui
    Network
    Sql
)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS
    Core
    Gui
    Network
    Sql
)

set(PROJECT_SOURCES
    aeathash.h
    aeathash.cpp
    aeatrecord.h
    aeatrecord.cpp
    aeatresponse.h
    aeatresponse.cpp
    aeatstore.h
    aeatstore.cpp
    aeatqr.h
    aeatqr.cpp
    aeatcertificate.h
    aeatcertificate.cpp
    aeattransport.h
    aeattransport.cpp
    aeatdirectbackend.h
    aeatdirectbackend.cpp
    aeatselftest.h
    aeatselftest.cpp
    ../third_party/qrcodegen/qrcodegen.hpp
    ../third_party/qrcodegen/qrcodegen.cpp
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
    verifactubackend.h
    verifactuintegration.h
    verifactuintegration.cpp
)

add_library(verifactu ${PROJECT_SOURCES})

target_link_libraries(verifactu
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Sql
    appsettings
)

if(WIN32)
    target_link_libraries(verifactu winhttp crypt32 ncrypt)
endif()

target_compile_definitions(verifactu PRIVATE
    PROJECT_VERSION="${PROJECT_VERSION}"
)

target_include_directories(verifactu
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/qrcodegen
)
