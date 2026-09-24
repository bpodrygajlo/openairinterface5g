# SPDX-License-Identifier: LicenseRef-CSSL-1.0

add_executable(test_rfsimulator test_rfsimulator.cpp)
target_link_libraries(test_rfsimulator PRIVATE rfsimulator_static UTIL CONFIG_LIB GTest::gtest dl shlib_loader)
add_dependencies(tests test_rfsimulator)
add_test(NAME test_rfsimulator COMMAND ./test_rfsimulator)
# Guard against a real regression reintroducing the connect-time hang this test covers:
# without a bound, a stuck client-connect handshake would spin forever instead of failing.
set_tests_properties(test_rfsimulator PROPERTIES TIMEOUT 10)

add_executable(test_rfsim_cirdb test_rfsim_cirdb.cpp ../rfsim_cirdb.c)
target_include_directories(test_rfsim_cirdb PRIVATE ..)
target_link_libraries(test_rfsim_cirdb PRIVATE cirdb UTIL GTest::gtest)
if (ENABLE_CHANNEL_SIM_CUDA)
  target_sources(test_rfsim_cirdb PRIVATE ../rfsim_cirdb_cuda.cu)
  target_compile_definitions(test_rfsim_cirdb PRIVATE CHANNEL_SIM_CUDA)
  target_link_libraries(test_rfsim_cirdb PRIVATE CUDA::cudart)
endif()
add_dependencies(tests test_rfsim_cirdb)
add_test(NAME test_rfsim_cirdb COMMAND ./test_rfsim_cirdb)
