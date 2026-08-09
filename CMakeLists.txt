cmake_minimum_required(VERSION 3.10)

project(rme)

option(ENABLE_UNITY_BUILD "Enable unity build for faster compilation" ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)

find_package(OpenGL REQUIRED)

# LibArchive support is disabled by default. To enable OTGZ support, define
# OTGZ_SUPPORT in source/definitions.h and add LibArchive back to the build.
# if(APPLE)
#     set(CMAKE_PREFIX_PATH /usr/local/opt/libarchive)
# endif()
# find_package(LibArchive REQUIRED)

find_package(wxWidgets COMPONENTS html aui gl adv core net base REQUIRED)

find_package(ZLIB REQUIRED)

include(${wxWidgets_USE_FILE})
include(source/CMakeLists.txt)
add_executable(${PROJECT_NAME} ${rme_H} ${rme_SRC})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD_REQUIRED ON)

include_directories(${OPENGL_INCLUDE_DIR} ${ZLIB_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME}
	${wxWidgets_LIBRARIES}
	${OPENGL_LIBRARIES}
	${ZLIB_LIBRARIES}
	glad::glad
	fmt::fmt
	nlohmann_json::nlohmann_json
)

if(ENABLE_UNITY_BUILD)
	if(CMAKE_VERSION VERSION_LESS 3.16)
		message(WARNING "ENABLE_UNITY_BUILD requires CMake 3.16 or newer; continuing without unity build")
	else()
		set_target_properties(${PROJECT_NAME} PROPERTIES
			UNITY_BUILD ON
			UNITY_BUILD_BATCH_SIZE 8
		)
	endif()
endif()
