# Run-time helper for sama_forbid_bgfx_in_target — invoked by CTest.
# Preprocesses PROBE_SRC against TARGET_INCLUDES and greps the output for
# "bgfx/bgfx.h".  Fails (with a message) if any include path drags it in.

if(NOT DEFINED PROBE_SRC OR NOT DEFINED PROBE_PP OR NOT DEFINED HEADER)
    message(FATAL_ERROR "sama_forbid_bgfx_run: missing required arg")
endif()
if(NOT DEFINED CXX_COMPILER OR NOT CXX_COMPILER)
    message(FATAL_ERROR "sama_forbid_bgfx_run: CXX_COMPILER not set")
endif()

# Build the -I flag list from the semicolon-separated include directories.
set(_inc_args "")
foreach(_dir ${TARGET_INCLUDES})
    if(_dir)
        list(APPEND _inc_args "-I${_dir}")
    endif()
endforeach()

# Preprocess only — no codegen, no linking.
execute_process(
    COMMAND ${CXX_COMPILER} -std=c++20 -x c++ -E ${_inc_args} ${PROBE_SRC}
    OUTPUT_FILE ${PROBE_PP}
    RESULT_VARIABLE _rc
    ERROR_VARIABLE _err
)

if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "sama_forbid_bgfx_run: preprocessor failed for ${HEADER}: ${_err}")
endif()

file(READ ${PROBE_PP} _pp_contents)
# bgfx's main public header is bgfx/bgfx.h — that's the file the proposal
# explicitly forbids transitively pulling into the boundary.
if(_pp_contents MATCHES "bgfx/bgfx\\.h")
    message(FATAL_ERROR
        "sama_forbid_bgfx_run: header '${HEADER}' transitively includes "
        "<bgfx/bgfx.h>.  Public engine headers must not leak bgfx into "
        "consumers — see docs/NOTES.md (bgfx abstraction policy)."
    )
endif()

message(STATUS "sama_forbid_bgfx_run: ${HEADER} is bgfx-clean")
