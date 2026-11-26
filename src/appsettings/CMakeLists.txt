add_library(appsettings STATIC
    appsettings.h
    appsettings.cpp
    applanguage.h
    applanguage.cpp
)
target_link_libraries(appsettings
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
)
# Windows DPAPI (CryptProtectData/CryptUnprotectData) for encrypting the Verifactu
# service key at rest in ~/.laideal_settings.json.
if(WIN32)
    target_link_libraries(appsettings PRIVATE crypt32)
endif()
target_include_directories(appsettings INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
