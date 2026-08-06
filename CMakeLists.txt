cmake_minimum_required(VERSION 3.14)
project(FTL_Engine C)

# Set C standard and enable strict warning flags for embedded code
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

# Tell the compiler where header files live (.h)
include_directories(Inc)

# Define the test executable and list the C files to compile together
add_executable(ftl_test_runner
    Src/ftl.c
    tests/mock_flash.c
    tests/test_ftl.c
)

# Enable CTest so 'ctest' command works on GitHub
enable_testing()
add_test(NAME RunAllFTLTests COMMAND ftl_test_runner)