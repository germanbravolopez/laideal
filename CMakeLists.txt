cmake_minimum_required(VERSION 3.5)
project(laideal VERSION 12.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set PROJECT_VERSION for the entire project
set(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})


find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Sql Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql PrintSupport Network Test)

add_subdirectory(src/logging)
add_subdirectory(src/appsettings)
add_subdirectory(src/reporthtml)
add_subdirectory(src/uikit)
add_subdirectory(src/app)
add_subdirectory(src/sql_lite)
add_subdirectory(src/tableview)
add_subdirectory(src/printing)
add_subdirectory(src/imprimir)
add_subdirectory(src/listado)
add_subdirectory(src/recog_prendas)
add_subdirectory(src/contabilidad)
add_subdirectory(src/facturas)
add_subdirectory(src/add_garment)
add_subdirectory(src/verifactu)
add_subdirectory(src/updater)
add_subdirectory(src/backup)

# UI smoke test build only (.github/workflows/ui-smoke.yml): compiles the --fake-aeat
# switch into laideal.exe. Never set for a release; the release job refuses such an exe.
option(LAIDEAL_UI_TEST "Build laideal.exe for the UI smoke test (--fake-aeat)" OFF)
if (LAIDEAL_UI_TEST)
    message(WARNING "LAIDEAL_UI_TEST is ON: this laideal.exe accepts --fake-aeat and must never be "
                    "installed at the shop. Use a separate build folder (build-uitest).")
    target_compile_definitions(laideal PRIVATE LAIDEAL_UI_TEST)
endif()

# Unit / integration tests (Qt Test + CTest). Run with `ctest` from build/ or
# `cmake --build build --target test`. CI and the release gate run them too.
enable_testing()
add_subdirectory(tests)
