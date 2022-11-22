add_library(sql_lite STATIC
    sql_lite.h
    sql_lite.cpp
)
target_link_libraries(sql_lite PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Sql)
target_include_directories(sql_lite INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
