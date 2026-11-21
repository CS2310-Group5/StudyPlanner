cmake_minimum_required(VERSION 3.20)
project(CityUStudyPlan LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)

add_executable(cityu_study_plan
    src/main.cpp
    src/models.cpp
    src/config.cpp
    src/runtime_paths.cpp
    src/json.cpp
    src/page_parser.cpp
    src/acquisition.cpp
    src/database_builder.cpp
    src/course_update_scan.cpp
    src/electives.cpp
    src/planner.cpp
    src/ui.cpp
    src/excel.cpp
    src/logging.cpp
)

target_include_directories(cityu_study_plan PRIVATE src)
target_link_libraries(cityu_study_plan PRIVATE CURL::libcurl)

configure_file(auth_helper.py ${CMAKE_CURRENT_BINARY_DIR}/auth_helper.py COPYONLY)
configure_file(excel_writer.py ${CMAKE_CURRENT_BINARY_DIR}/excel_writer.py COPYONLY)
configure_file(config.json ${CMAKE_CURRENT_BINARY_DIR}/config.json COPYONLY)
configure_file(courses_db.json ${CMAKE_CURRENT_BINARY_DIR}/courses_db.json COPYONLY)
configure_file(ge_areas.json ${CMAKE_CURRENT_BINARY_DIR}/ge_areas.json COPYONLY)

include(CTest)
if(BUILD_TESTING)
  enable_testing()
  add_executable(tests
      tests/test_main.cpp
      src/models.cpp
      src/config.cpp
      src/runtime_paths.cpp
      src/json.cpp
      src/page_parser.cpp
      src/acquisition.cpp
      src/database_builder.cpp
      src/electives.cpp
      src/planner.cpp
      src/ui.cpp
  )
  target_include_directories(tests PRIVATE src)
  target_link_libraries(tests PRIVATE CURL::libcurl)
  add_test(NAME cityu_unit_tests COMMAND tests)
  if(WIN32)
    set(STUDYPLAN_VENV_PYTHON "${CMAKE_CURRENT_SOURCE_DIR}/.venv/Scripts/python.exe")
  else()
    set(STUDYPLAN_VENV_PYTHON "${CMAKE_CURRENT_SOURCE_DIR}/.venv/bin/python")
  endif()
  if(EXISTS "${STUDYPLAN_VENV_PYTHON}")
    set(STUDYPLAN_TEST_PYTHON "${STUDYPLAN_VENV_PYTHON}")
  else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(STUDYPLAN_TEST_PYTHON "${Python3_EXECUTABLE}")
  endif()
  execute_process(
      COMMAND "${STUDYPLAN_TEST_PYTHON}" -c "import openpyxl"
      RESULT_VARIABLE STUDYPLAN_OPENPYXL_RESULT
      OUTPUT_QUIET ERROR_QUIET)
  if(NOT STUDYPLAN_OPENPYXL_RESULT EQUAL 0)
    message(FATAL_ERROR "The demo Excel test requires openpyxl in ${STUDYPLAN_TEST_PYTHON}")
  endif()
  add_test(NAME cityu_demo_excel
      COMMAND "${STUDYPLAN_TEST_PYTHON}"
              "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_demo_excel.py"
              "$<TARGET_FILE:cityu_study_plan>"
              "${CMAKE_CURRENT_BINARY_DIR}/test-user-data"
              "${CMAKE_CURRENT_SOURCE_DIR}/courses_db.json")
  add_test(NAME cityu_interactive_flow
      COMMAND "${STUDYPLAN_TEST_PYTHON}"
              "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_interactive_flow.py"
              "$<TARGET_FILE:cityu_study_plan>"
              "${CMAKE_CURRENT_BINARY_DIR}/test-interactive-user-data"
              "${CMAKE_CURRENT_SOURCE_DIR}/courses_db.json")
endif()
