add_executable(rf_gpu_info main.cpp)

target_link_libraries(rf_gpu_info
    PRIVATE
        rf::gpu
        rf::warnings)
