add_library(appsettings STATIC
    appsettings.h
    appsettings.cpp
    settingsdialog.h
    settingsdialog.cpp
)
target_link_libraries(appsettings
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
)
target_include_directories(appsettings INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
