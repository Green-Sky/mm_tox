cmake_minimum_required(VERSION 3.2 FATAL_ERROR)

project(mm_tox CXX C)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# TODO: allow system installed toxcore
include(./toxcore.cmake)

#####################################

add_library(mm_tox
	./src/mm_tox/services/tox_service.hpp
	./src/mm_tox/services/tox_service.cpp

	./src/mm_tox/services/tox_net_channeled.hpp
	./src/mm_tox/services/tox_net_channeled.cpp
)

target_link_libraries(mm_tox
	engine
	filesystem_service

	toxcore
	#sodium
)

target_include_directories(mm_tox PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

#####################################

add_library(mm_tox_imgui
	./src/mm_tox/imgui/widgets/tox.hpp
	./src/mm_tox/imgui/widgets/tox.cpp

	./src/mm_tox/services/tox_chat.hpp
	./src/mm_tox/services/tox_chat.cpp
)

target_link_libraries(mm_tox_imgui
	engine

	mm_tox

	imgui_service
)

target_include_directories(mm_tox_imgui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

