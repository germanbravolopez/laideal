add_library(settingsdialog STATIC
    settingsdialog.h
    settingsdialog.cpp
)
target_link_libraries(settingsdialog
    PUBLIC
    Qt${QT_VERSION_MAJOR}::Widgets
    appsettings
    PRIVATE
    Qt${QT_VERSION_MAJOR}::PrintSupport   # QPrinterInfo: list installed printers
    uikit                                 # setUpDialog, primary button, date field
    printing                              # StatusApiPrinter: "Probar Status API" diagnostic button
)
target_include_directories(settingsdialog INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
