# SPDX-License-Identifier: LicenseRef-CSSL-1.0

set(DPDK_TEST_ARGS "--no-huge" "--iova-mode=va" "--no-pci" "-m" "1024")

add_executable(test_oru_io test_oru_io.c)
target_link_libraries(test_oru_io PRIVATE oru_io ${dpdk_LIBRARIES})
target_include_directories(test_oru_io PRIVATE ${dpdk_INCLUDE_DIRS})
add_dependencies(tests test_oru_io)
add_test(NAME test_oru_io COMMAND test_oru_io ${DPDK_TEST_ARGS} "--file-prefix=test_oru_io" "--vdev=net_null0" "--vdev=net_null1")
set_property(TEST test_oru_io PROPERTY LABELS fronthaul)

add_executable(test_oru_packet_processor test_oru_packet_processor.c)
target_link_libraries(test_oru_packet_processor PRIVATE oru_packet_processor log_headers ${dpdk_LIBRARIES})
target_include_directories(test_oru_packet_processor PRIVATE ${dpdk_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/../../xran_pkt)
add_dependencies(tests test_oru_packet_processor)
add_test(NAME test_oru_packet_processor COMMAND test_oru_packet_processor ${DPDK_TEST_ARGS} "--file-prefix=test_oru_packet_processor" "--vdev=net_null0" "--vdev=net_null1")
set_property(TEST test_oru_packet_processor PROPERTY LABELS fronthaul)
