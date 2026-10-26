cmake_minimum_required(VERSION 3.10)

# Project metadata
project(Samsung300kTool VERSION 1.2.1 LANGUAGES C)

# Linux only: relies on sysfs for Samsung VID detection.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "Samsung300kTool4Linux isn't supported on your system. \n "
        "Detected: ${CMAKE_SYSTEM_NAME}")
endif()

# C99 for POSIX functions (usleep, sigaction, etc.)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Warnings and optimization
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -Wextra -O2)
endif()

# Build the final binary
add_executable(samsung-300k-tool samsung-300k-tool.c)

# Pass the project version to the C source, so --version stays in sync
target_compile_definitions(samsung-300k-tool PRIVATE
    TOOL_VERSION_CMAKE="${PROJECT_VERSION}"
)

# Install to <prefix>/bin (default /usr/local/bin)
install(TARGETS samsung-300k-tool
    RUNTIME DESTINATION bin
)
