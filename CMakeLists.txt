cmake_minimum_required(VERSION 3.16...3.27)
project(gstrerunsink VERSION 0.2 LANGUAGES CXX)

# ==================== OPTIONS ====================
option(WITH_NVMM_SUPPORT "Enable NVMM (NVIDIA Multi Media) buffer support" OFF)

# ==================== DEPENDENCIES ====================
find_package(PkgConfig REQUIRED)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
FetchContent_Declare(rerun_sdk URL
    https://github.com/rerun-io/rerun/releases/download/0.24.0/rerun_cpp_sdk.zip)
FetchContent_Populate(rerun_sdk)

execute_process(
    COMMAND ${CMAKE_COMMAND} -E echo "Patching rerun_sdk..."
)

execute_process(
    COMMAND patch -p1 -i ${CMAKE_CURRENT_SOURCE_DIR}/rerun_sdk.patch
    WORKING_DIRECTORY ${rerun_sdk_SOURCE_DIR}
)

add_subdirectory(${rerun_sdk_SOURCE_DIR} ${rerun_sdk_BINARY_DIR})

pkg_check_modules(GST REQUIRED
    gstreamer-1.0
    gstreamer-base-1.0
    gstreamer-video-1.0
    gstreamer-check-1.0
)


# ==================== NVIDIA DEPENDENCIES (OPTIONAL) ====================
if(WITH_NVMM_SUPPORT)
    find_path(CUDA_INCLUDE_DIR cuda_runtime.h PATHS /usr/local/cuda/include)
    find_library(CUDA_CUDART_LIBRARY cudart PATHS /usr/local/cuda/lib64)
    find_library(NVBUF_LIB nvbufsurface PATHS /opt/nvidia/deepstream/deepstream-6.3/lib)

    if (NOT CUDA_INCLUDE_DIR OR NOT CUDA_CUDART_LIBRARY)
        message(FATAL_ERROR "Could not find CUDA. Please check that it's installed and paths are correct.")
    endif()

    if(NOT NVBUF_LIB)
        message(FATAL_ERROR "Could not find libnvbufsurface.so. Please install DeepStream SDK.")
    endif()
    
    message(STATUS "NVMM support: ENABLED")
    message(STATUS "  CUDA include: ${CUDA_INCLUDE_DIR}")
    message(STATUS "  CUDA library: ${CUDA_CUDART_LIBRARY}")
    message(STATUS "  NVBuffer library: ${NVBUF_LIB}")
else()
    message(STATUS "NVMM support: DISABLED")
endif()

# ==================== INCLUDE DIRECTORIES ====================
include_directories(${GST_INCLUDE_DIRS})

if(WITH_NVMM_SUPPORT)
    include_directories(${CUDA_INCLUDE_DIR})
    include_directories(/opt/nvidia/deepstream/deepstream-6.3/sources/includes/)
endif()

# ==================== PLUGIN TARGET ====================
add_library(rerunsink MODULE src/gstrerunsink.cpp)

# Include directories
target_include_directories(rerunsink PRIVATE ${GST_INCLUDE_DIRS})

# Compile definitions
target_compile_definitions(rerunsink PRIVATE PACKAGE="rerunsink")
if(WITH_NVMM_SUPPORT)
    target_compile_definitions(rerunsink PRIVATE HAVE_NVMM_SUPPORT)
endif()

# Compile options
target_compile_options(rerunsink PRIVATE ${GST_CFLAGS_OTHER} -fvisibility=default)

# Link libraries
target_link_libraries(rerunsink PRIVATE ${GST_LIBRARIES} rerun_sdk)
if(WITH_NVMM_SUPPORT)
    target_link_libraries(rerunsink PRIVATE ${CUDA_CUDART_LIBRARY} ${NVBUF_LIB})
endif()

# Set properties
set_target_properties(rerunsink PROPERTIES PREFIX "libgst")

# ==================== INSTALLATION ====================
# Install the plugin to the GStreamer plugin directory
install(TARGETS rerunsink
    LIBRARY DESTINATION lib/gstreamer-1.0
)

# ==================== SUMMARY ====================
message(STATUS "")
message(STATUS "Configuration Summary:")
message(STATUS "  Project: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  NVMM support: ${WITH_NVMM_SUPPORT}")
message(STATUS "")

