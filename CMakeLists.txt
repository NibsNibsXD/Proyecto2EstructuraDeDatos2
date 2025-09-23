cmake_minimum_required(VERSION 3.16)
project(miniaccess VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Auto Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# ---- helper: añade a una lista solo los paths que existan ----
function(collect_existing out_var)
  set(tmp "")
  foreach(p ${ARGN})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${p}")
      list(APPEND tmp "${p}")
    endif()
  endforeach()
  set(${out_var} "${tmp}" PARENT_SCOPE)
endfunction()

# ============================================================
# LIBRERÍA COMÚN (TablesPage + núcleo de datos)
# ============================================================
add_library(pages STATIC
  tablespage.cpp
  tablespage.h
  datamodel.cpp
  datamodel.h
)
target_link_libraries(pages PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
# Para que otros targets encuentren los headers (tablespage.h, datamodel.h)
target_include_directories(pages PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# ============================================================
# A) App integrada (opcional)
# ============================================================
collect_existing(APP_SRCS
  app_miniaccess/main.cpp         main.cpp
  app_miniaccess/mainwindow.cpp   mainwindow.cpp
  app_miniaccess/mainwindow.h     mainwindow.h
  app_miniaccess/mainwindow.ui    mainwindow.ui
)
if(APP_SRCS)
  add_executable(app_miniaccess ${APP_SRCS})
  target_link_libraries(app_miniaccess PRIVATE Qt${QT_VERSION_MAJOR}::Widgets pages)
  target_include_directories(app_miniaccess PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
endif()

# ============================================================
# B) Sandbox Shell (Persona A)  ← incluye RecordsPage (Persona C)
# ============================================================
collect_existing(SHELL_SRCS
  shell_sandbox/shell_sandbox.cpp     shell_sandbox.cpp
  shell_sandbox/shellwindow.cpp       shellwindow.cpp
  shell_sandbox/shellwindow.h         shellwindow.h
  shell_sandbox/resources_shell.qrc   resources_shell.qrc
  shell_sandbox/shellwindow.ui        shellwindow.ui

  # Reutilizamos la página de Registros dentro del shell
  data_sandbox/recordspage.cpp        recordspage.cpp
  data_sandbox/recordspage.h          recordspage.h
  data_sandbox/Forms/recordspage.ui   recordspage.ui
  data_sandbox/resources_records.qrc  resources_records.qrc
)
if(SHELL_SRCS)
  add_executable(shell_sandbox ${SHELL_SRCS}
    querypage.h querypage.cpp
    querystore.h querystore.cpp
    querydesigner.h querydesigner.cpp
    accessquerydesigner.h accessquerydesigner.cpp)
  target_link_libraries(shell_sandbox PRIVATE Qt${QT_VERSION_MAJOR}::Widgets pages)
  # Para que shellwindow.cpp pueda incluir "recordspage.h"
  target_include_directories(shell_sandbox PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/data_sandbox
  )
endif()

# ============================================================
# C) Sandbox Tablas (Persona B)
# ============================================================
collect_existing(TABLES_SRCS
  tables_sandbox/tables_sandbox.cpp   tables_sandbox.cpp
  tables_sandbox/resources_tables.qrc resources_tables.qrc
  tables_sandbox/tablespage.ui        tablespage.ui   # solo si existe
)
if(TABLES_SRCS)
  add_executable(tables_sandbox ${TABLES_SRCS})
  target_link_libraries(tables_sandbox PRIVATE Qt${QT_VERSION_MAJOR}::Widgets pages)
  target_include_directories(tables_sandbox PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
endif()

# ============================================================
# D) Sandbox Registros (Persona C)
# ============================================================
collect_existing(DATA_SRCS
  data_sandbox/data_sandbox.cpp       data_sandbox.cpp
  data_sandbox/recordspage.cpp        recordspage.cpp
  data_sandbox/recordspage.h          recordspage.h
  data_sandbox/resources_records.qrc  resources_records.qrc
  data_sandbox/Forms/recordspage.ui   recordspage.ui
)
if(DATA_SRCS)
  add_executable(data_sandbox ${DATA_SRCS})
  target_link_libraries(data_sandbox PRIVATE Qt${QT_VERSION_MAJOR}::Widgets pages)
  target_include_directories(data_sandbox PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/data_sandbox
  )
endif()
