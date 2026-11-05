find_program(SH sh REQUIRED)
set(mock "${CMAKE_CURRENT_BINARY_DIR}/distcc-launcher-test/mock-distcc.sh")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/distcc-launcher-test")
file(WRITE "${mock}" "#!/bin/sh\nprintf '%s\\n' \"$@\"\nexit 7\n")
file(CHMOD "${mock}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

execute_process(
    COMMAND "${SH}" "${LAUNCHER}" "${mock}" "/some compiler/path/c++"
        "-DNAME=hello world" "input file.cpp" -o "output file.o"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    RESULT_VARIABLE result)

if(NOT result EQUAL 7)
    message(FATAL_ERROR "Launcher did not preserve the compiler exit code: ${result}; ${error}")
endif()
if(NOT output STREQUAL "c++\n-DNAME=hello world\ninput file.cpp\n-o\noutput file.o\n")
    message(FATAL_ERROR "Launcher did not strip the compiler path or preserve arguments: ${output}")
endif()
