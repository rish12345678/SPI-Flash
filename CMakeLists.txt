cmake_minimum_required(VERSION 3.14)
project(FTL_Engine C)

# Set C standard and enable strict warning flags for embedded code
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

# Declare the FORCE_FLASH_RESET option (default is OFF)
option(FORCE_FLASH_RESET "Enable forced flash chip reset on startup" OFF)

# Tell the compiler where header files live (.h)
include_directories(Inc)

# Define the test executable and list the C files to compile together
add_executable(ftl_test_runner
    Src/ftl.c
    tests/mock_flash.c
    tests/test_ftl.c
)

# Pass -DFORCE_FLASH_RESET preprocessor macro to compiler if flag is ON
if(FORCE_FLASH_RESET)
    target_compile_definitions(ftl_test_runner PRIVATE FORCE_FLASH_RESET)
endif()

# Enable CTest so 'ctest' command works on GitHub
enable_testing()
add_test(NAME RunAllFTLTests COMMAND ftl_test_runner)