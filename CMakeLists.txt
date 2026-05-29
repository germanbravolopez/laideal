cmake_minimum_required(VERSION 3.5)
project(laideal VERSION 8.4 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set PROJECT_VERSION for the entire project
set(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})


find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Sql Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql PrintSupport Network)
#find_package(QXlsxQt6 REQUIRED)

add_subdirectory(QXlsx)
add_subdirectory(src/logging)
add_subdirectory(src/appsettings)
add_subdirectory(src/app)
add_subdirectory(src/sql_lite)
add_subdirectory(src/tableview)
add_subdirectory(src/imprimir)
add_subdirectory(src/listado)
add_subdirectory(src/recog_prendas)
add_subdirectory(src/contabilidad)
add_subdirectory(src/facturas)
add_subdirectory(src/add_garment)
add_subdirectory(src/verifactu)
add_subdirectory(src/updater)
