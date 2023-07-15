cmake_minimum_required(VERSION 3.5)

project(laideal VERSION 0.4 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql PrintSupport)
#find_package(QXlsxQt6 REQUIRED)

add_subdirectory(QXlsx)
add_subdirectory(src/app)
add_subdirectory(src/sql_lite)
add_subdirectory(src/MySortFilterProxyModel)
add_subdirectory(src/TableView)
add_subdirectory(src/NumberFormatDelegate)
add_subdirectory(src/gastos)
add_subdirectory(src/imprimir)
add_subdirectory(src/ingresos)
add_subdirectory(src/Listado)
add_subdirectory(src/recog_prendas)
add_subdirectory(src/contabilidad)
add_subdirectory(src/facturas)
