cmake_minimum_required(VERSION 3.10)

# Project metadata
project(Samsung300kTool VERSION 1.0.0 LANGUAGES C)

# Use standard C99 for POSIX functions compatibility (like usleep)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Secure and optimized compilation flags
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -Wextra -O2)
endif()

# Build the final binary from source
add_executable(samsung-300k-tool samsung-300k-tool.c)

# Installation rule for Unix-like environments (/usr/local/bin)
install(TARGETS samsung-300k-tool DESTINATION /usr/local/bin)
