add_library(reporthtml STATIC
    reporthtml.h
    reporthtml.cpp
)
target_link_libraries(reporthtml
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::PrintSupport
    appsettings
)
target_include_directories(reporthtml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
