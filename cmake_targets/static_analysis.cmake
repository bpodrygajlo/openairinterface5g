# AddStaticAnalysis.cmake

# -------------------------------------------------------------------------
# CREATE THE CENTRALIZED MASTER TARGET (Runs once when module is included)
# -------------------------------------------------------------------------
if(NOT TARGET run_static_analysis)
    add_custom_target(run_static_analysis
        COMMAND ${CMAKE_COMMAND} -E echo "=================================================="
        COMMAND ${CMAKE_COMMAND} -E echo " Starting global project-wide static analysis... "
        COMMAND ${CMAKE_COMMAND} -E echo "=================================================="
        COMMENT "Executing master static analysis pipeline"
    )
endif()

if(NOT TARGET run_cppcheck)
    add_custom_target(run_cppcheck
        COMMAND ${CMAKE_COMMAND} -E echo "Running cppcheck..."
        COMMENT "Executing project-wide cppcheck"
    )
endif()

if(NOT TARGET run_clang_tidy)
    add_custom_target(run_clang_tidy
        COMMAND ${CMAKE_COMMAND} -E echo "Running clang-tidy..."
        COMMENT "Executing project-wide clang-tidy"
    )
endif()

macro(add_static_analysis_to_target TARGET_NAME)
    set(OPTIONS CPPCHECK CLANG_TIDY)
    cmake_parse_arguments(ARG "" "" "${OPTIONS}" ${ARGN})

    if(NOT ARG_CPPCHECK AND NOT ARG_CLANG_TIDY)
        set(ARG_CPPCHECK TRUE)
        set(ARG_CLANG_TIDY TRUE)
    endif()

    # Extract the source files belonging to this specific target
    get_target_property(TARGET_SOURCES ${TARGET_NAME} SOURCES)
    if(NOT TARGET_SOURCES)
        return()
    endif()

    # Filter out non-C/C++ files from the target's source list
    set(LINT_FILES "")
    foreach(SOURCE_FILE ${TARGET_SOURCES})
        if(SOURCE_FILE MATCHES "\\.(cpp|c|cc|h|hpp)$")
            if(NOT IS_ABSOLUTE "${SOURCE_FILE}")
                set(SOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE_FILE}")
            endif()
            list(APPEND LINT_FILES "${SOURCE_FILE}")
        endif()
    endforeach()

    # Define unique child target names for this specific target
    set(LOCAL_ANALYSIS_TARGET "check_${TARGET_NAME}")
    set(LOCAL_CPPCHECK_TARGET "check_${TARGET_NAME}_cppcheck")
    set(LOCAL_CLANG_TIDY_TARGET "check_${TARGET_NAME}_clang_tidy")

# 1. Setup Cppcheck
    if(ARG_CPPCHECK)
        find_program(CPPCHECK_EXE NAMES "cppcheck")
        if(CPPCHECK_EXE)
            # Build --file-filter= flags to restrict analysis to this target's files.
            # cppcheck reads compilation flags from compile_commands.json (--project),
            # so no manual -I flags are needed.
            set(CPPCHECK_FILE_FILTERS "")
            foreach(SRC ${LINT_FILES})
                list(APPEND CPPCHECK_FILE_FILTERS "--file-filter=${SRC}")
            endforeach()

            set(CPPCHECK_SUPPRESSION_ARGS "")
            if(EXISTS "${CMAKE_SOURCE_DIR}/.cppcheck-suppressions")
                list(APPEND CPPCHECK_SUPPRESSION_ARGS
                    "--suppressions-list=${CMAKE_SOURCE_DIR}/.cppcheck-suppressions")
            endif()

            add_custom_target(${LOCAL_CPPCHECK_TARGET}
                COMMAND "${CPPCHECK_EXE}"
                    "--project=${CMAKE_BINARY_DIR}/compile_commands.json"
                    "--enable=all"
                    "--inline-suppr"
                    "--suppress=missingIncludeSystem"
                    ${CPPCHECK_SUPPRESSION_ARGS}
                    ${CPPCHECK_FILE_FILTERS}
                COMMENT "cppcheck: ${TARGET_NAME}"
                VERBATIM
            )
            add_dependencies(${LOCAL_CPPCHECK_TARGET} asn1_nr_rrc asn1_lpp asn1_lte_rrc s1ap asn1_nrppa m3ap x2ap xnap m2ap f1ap e1ap log_headers)
            add_dependencies(run_cppcheck ${LOCAL_CPPCHECK_TARGET})
        else()
            get_property(CPPCHECK_WARNED GLOBAL PROPERTY HAS_WARNED_CPPCHECK)
            if(NOT CPPCHECK_WARNED)
                message(WARNING "cppcheck executable not found. Skipping cppcheck.")
                set_property(GLOBAL PROPERTY HAS_WARNED_CPPCHECK TRUE)
            endif()
        endif()
    endif()

    # 2. Setup Clang-Tidy
    if(ARG_CLANG_TIDY)
        find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
        if(CLANG_TIDY_EXE)
            # Point explicitly at the repo-root .clang-tidy so the config is
            # predictable regardless of where clang-tidy is invoked from.
            set(CLANG_TIDY_CONFIG_ARGS "")
            if(EXISTS "${CMAKE_SOURCE_DIR}/.clang-tidy")
                list(APPEND CLANG_TIDY_CONFIG_ARGS
                    "--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy")
            endif()

            add_custom_target(${LOCAL_CLANG_TIDY_TARGET}
                COMMAND "${CLANG_TIDY_EXE}"
                    "-p=${CMAKE_BINARY_DIR}"
                    "-extra-arg=-Wno-unknown-warning-option"
                    "-extra-arg=-Wno-unused-command-line-argument"
                    ${CLANG_TIDY_CONFIG_ARGS}
                    ${LINT_FILES}
                COMMENT "clang-tidy: ${TARGET_NAME}"
                VERBATIM
            )
            add_dependencies(${LOCAL_CLANG_TIDY_TARGET} asn1_nr_rrc asn1_lpp asn1_lte_rrc s1ap asn1_nrppa m3ap x2ap xnap m2ap f1ap e1ap log_headers)
            add_dependencies(run_clang_tidy ${LOCAL_CLANG_TIDY_TARGET})
        else()
            get_property(CLANG_TIDY_WARNED GLOBAL PROPERTY HAS_WARNED_CLANG_TIDY)
            if(NOT CLANG_TIDY_WARNED)
                message(WARNING "clang-tidy executable not found. Skipping clang-tidy.")
                set_property(GLOBAL PROPERTY HAS_WARNED_CLANG_TIDY TRUE)
            endif()
        endif()
    endif()

    # 3. Create the combined per-target child and attach it to the master target
    set(LOCAL_DEPS "")
    if(TARGET ${LOCAL_CPPCHECK_TARGET})
        list(APPEND LOCAL_DEPS ${LOCAL_CPPCHECK_TARGET})
    endif()
    if(TARGET ${LOCAL_CLANG_TIDY_TARGET})
        list(APPEND LOCAL_DEPS ${LOCAL_CLANG_TIDY_TARGET})
    endif()

    if(LOCAL_DEPS)
        add_custom_target(${LOCAL_ANALYSIS_TARGET})
        add_dependencies(${LOCAL_ANALYSIS_TARGET} ${LOCAL_DEPS})
        add_dependencies(run_static_analysis ${LOCAL_ANALYSIS_TARGET})
    endif()

endmacro()
