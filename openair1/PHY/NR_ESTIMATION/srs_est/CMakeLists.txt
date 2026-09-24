# SPDX-License-Identifier: LicenseRef-CSSL-1.0

# SRS channel estimation module, loaded with --loader.srs_est.shlibversion _cuda

add_boolean_option(ENABLE_SRS_CUDA OFF "Build the CUDA SRS channel estimation module (libsrs_est_cuda.so)" OFF)
if (ENABLE_SRS_CUDA)
  enable_language(CUDA)
  find_package(CUDAToolkit REQUIRED)
  add_library(srs_est_cuda MODULE srs_est_cuda.cu)
  # cuFFT and cuSOLVER are there if you need them
  target_link_libraries(srs_est_cuda PRIVATE CUDA::cudart CUDA::cufft CUDA::cusolver)
  set_target_properties(srs_est_cuda PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

  if (ENABLE_TESTS)
    add_executable(test_srs_est tests/test_srs_est.cpp)
    target_include_directories(test_srs_est PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_srs_est PRIVATE GTest::gtest GTest::gtest_main dl)
    add_dependencies(test_srs_est srs_est_cuda)
    add_dependencies(tests test_srs_est)
    # the module is loaded from the build directory
    add_test(NAME test_srs_est COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_srs_est WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
  endif()
endif()
