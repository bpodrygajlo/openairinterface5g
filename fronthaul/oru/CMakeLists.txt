# SPDX-License-Identifier: LicenseRef-CSSL-1.0

add_library(oru_io oru_io.c)
target_include_directories(oru_io PUBLIC ./)
target_link_libraries(oru_io PUBLIC fh_core ${dpdk_LIBRARIES})
target_include_directories(oru_io PRIVATE ${dpdk_INCLUDE_DIRS})

if (ENABLE_TESTS)
    add_subdirectory(tests)
endif()
