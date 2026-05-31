add_library(backup STATIC
    backup_manager.h
    backup_manager.cpp
)
target_link_libraries(backup
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Sql
    appsettings
    sql_lite
)
target_include_directories(backup PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
