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
    aeatchaincheck.h
    aeatchaincheck.cpp
    aeatqr.h
    aeatqr.cpp
    aeatcertificate.h
    aeatcertificate.cpp
    aeattransport.h
    aeattransport.cpp
    testhostguard.h
    aeatdirectbackend.h
    aeatdirectbackend.cpp
    aeatselftest.h
    aeatselftest.cpp
    producerdeclaration.h
    producerdeclaration.cpp
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

# Header-only: lets sql_lite, add_garment, facturas and printing use VerifactuResult, the
# estado helpers and splitGross without linking the verifactu library.
add_library(verifactu_types INTERFACE)
target_include_directories(verifactu_types INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(verifactu_types INTERFACE Qt${QT_VERSION_MAJOR}::Gui)   # QPixmap

add_library(verifactu STATIC ${PROJECT_SOURCES})

# Qt: the public headers expose QString, QPixmap and QSqlDatabase. appsettings is PUBLIC: the
# tests include appsettings.h through verifactu. It links printing (StatusApiPrinter, the
# "Probar Status API" diagnostic button in SettingsDialog), so anything linking verifactu
# also links appsettings -> printing, although printing is otherwise used only by imprimir.
target_link_libraries(verifactu
    PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Sql
    appsettings
)

if(WIN32)
    target_link_libraries(verifactu PRIVATE winhttp crypt32 ncrypt)
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
