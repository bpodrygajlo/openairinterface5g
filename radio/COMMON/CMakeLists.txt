# SPDX-License-Identifier: LicenseRef-CSSL-1.0

add_library(radio_common STATIC common_lib.c record_player.c)
target_include_directories(radio_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(radio_common PUBLIC dl log_headers shlib_loader)

# UE-centric 7.1 functional split device
add_library(ue_split7 STATIC ue_split7_device.c)
target_link_libraries(ue_split7 PUBLIC radio_common)
