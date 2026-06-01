add_library(recog_prendas STATIC
    recog_prendas.ui
    recog_prendas.h
    recog_prendas.cpp
    pay_dialog.h
    pay_dialog.cpp
)
target_link_libraries(recog_prendas
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    sql_lite
    imprimir
    tableview
    verifactu
    appsettings
)
target_include_directories(recog_prendas INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
