cmake_minimum_required(VERSION 3.16)
project(miniaccess VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)   # bien para Q_OBJECT, pero NO usamos AUTOUIC
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# A) App integrada (opcional)
add_executable(app_miniaccess
  main.cpp
  mainwindow.cpp
  mainwindow.h
)
target_link_libraries(app_miniaccess PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# B) Sandbox Shell (Persona A)
add_executable(shell_sandbox
  shell_sandbox.cpp
  shellwindow.cpp
  shellwindow.h
)
target_link_libraries(shell_sandbox PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# C) Sandbox Tablas (Persona B)
add_executable(tables_sandbox
  tables_sandbox.cpp
  tablespage.cpp
  tablespage.h
)
target_link_libraries(tables_sandbox PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# D) Sandbox Registros (Persona C)
add_executable(data_sandbox
  data_sandbox.cpp
  recordspage.cpp
  recordspage.h
)
target_link_libraries(data_sandbox PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
