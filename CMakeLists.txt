cmake_minimum_required(VERSION 3.16)

set(API_ROOT "$ENV{API_ROOT}" CACHE FILEPATH "directory of the WinCC_OA API installation")

include(${API_ROOT}/CMakeDefines.txt)

project(streamingEWO)

set(TARGET streamingEWO)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
streamingEWO.cxx
)

if ( WIN32 )
  set (SOURCES ${SOURCES} VersInfo.rc)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets WebSockets)
qt_standard_project_setup()

# qt_add_executable(${TARGET} ${SOURCES})
qt_add_library(${TARGET} SHARED ${SOURCES})
target_link_libraries(${TARGET} PRIVATE Qt6::Widgets Qt6::WebSockets ewo)

set(DEST_DIR "C:/WinCC_OA_Proj/RTX_3.20/bin/widgets/windows-64")

# Create the directory at configure time (safe even if it exists)
file(MAKE_DIRECTORY "${DEST_DIR}")

add_custom_command(TARGET streamingEWO POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:streamingEWO>"
            "${DEST_DIR}/$<TARGET_FILE_BASE_NAME:streamingEWO>.ewo"
    COMMENT "Copying streamingEWO.dll to .ewo in WinCC OA folder"
)