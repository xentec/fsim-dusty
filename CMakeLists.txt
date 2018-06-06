cmake_minimum_required(VERSION 3.8 FATAL_ERROR)

project(fsim-dusty VERSION 0.1.0 LANGUAGES CXX)

# Options
#nothing to see here yet

set(SOURCES
	src/main.cpp
	src/global.hpp

	src/type/itr.hpp
	src/type/num.hpp
	src/type/ptr.hpp

	src/buffer.hpp
	src/logger.hpp

	src/sds011.hpp
	src/sds011.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
	INTERPROCEDURAL_OPTIMIZATION $<IF:$<CONFIG:Debug>,OFF,ON>
)
target_compile_features(${PROJECT_NAME} PUBLIC "cxx_std_17")
target_compile_options(${PROJECT_NAME} PUBLIC "-Wall")
target_compile_definitions(${PROJECT_NAME} PUBLIC "_POSIX_C_SOURCE=200809L" "ASIO_STANDALONE=1")

# Libraries
find_package(Threads REQUIRED)
add_subdirectory(extern/spdlog EXCLUDE_FROM_ALL)
add_subdirectory(extern/argh EXCLUDE_FROM_ALL)

target_include_directories(${PROJECT_NAME} PRIVATE "src")
target_link_libraries(${PROJECT_NAME}
	PUBLIC
	# static
		argh
		spdlog
	# dynamic
		${CMAKE_THREAD_LIBS_INIT}

)


# install
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
