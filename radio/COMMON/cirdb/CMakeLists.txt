# SPDX-License-Identifier: LicenseRef-CSSL-1.0

# CIR database access, shared by vrtsim and the rfsimulator
find_package(yaml-cpp REQUIRED)
add_library(cirdb STATIC cirdb_yaml.cpp cirdb_db.c)
set_target_properties(cirdb PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(cirdb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cirdb PUBLIC log_headers yaml-cpp)
