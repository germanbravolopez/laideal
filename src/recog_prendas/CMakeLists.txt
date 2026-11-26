add_library(recog_prendas STATIC
    recog_prendas.h
    recog_prendas.cpp
    aeatreconcilesummary.h
    aeatreconcilesummary.cpp
    invoicingpolicy.cpp
    pay_dialog.h
    pay_dialog.cpp
    voidgarmentsdialog.h
    voidgarmentsdialog.cpp
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
    uikit
)
target_link_libraries(recog_prendas PUBLIC add_garment)   # recog_prendas.h names AddGarment::InvoicingPolicy
target_include_directories(recog_prendas INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
