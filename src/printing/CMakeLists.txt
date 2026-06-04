# Direct ESC/POS thermal printing - replaces the old Excel/QXlsx/cscript path.
# EscPosBuilder + TicketRenderer are pure (no I/O, no GUI event loop) so they are
# unit-tested in tests/test_escpos.cpp; ThermalPrinter is the only Win32 piece
# (RAW spooler) and compiles to a failing stub off Windows so the rest stays
# cross-platform.
add_library(printing STATIC
    escposbuilder.h
    escposbuilder.cpp
    thermalprinter.h
    thermalprinter.cpp
    ticketrenderer.h
    ticketrenderer.cpp
    printerstatus.h
    printerstatus.cpp
    statusapiprinter.h
    statusapiprinter.cpp
)
# Qt::Gui (QImage) is exposed by the public headers (TicketData::qr / rasterImage).
target_link_libraries(printing PUBLIC
    Qt${QT_VERSION_MAJOR}::Gui
)
# winspool: OpenPrinter / StartDocPrinter (RAW) / WritePrinter. Ships with Windows.
if(WIN32)
    target_link_libraries(printing PRIVATE winspool)
endif()
target_include_directories(printing INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
