cmake_minimum_required(VERSION 4.0)

project(Muduo_ChatServer_Cluster)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
# Set flags for C++ compilation
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -g)
# Set the output directory for the compiled bin
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
#set include directories
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/thirdpart)
include_directories(${PROJECT_SOURCE_DIR}/include/database)
include_directories(${PROJECT_SOURCE_DIR}/include/public)
include_directories(${PROJECT_SOURCE_DIR}/include/server)
#load subdirectories
add_subdirectory(src)
#

