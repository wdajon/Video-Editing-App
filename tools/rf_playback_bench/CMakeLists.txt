add_executable(rf_playback_bench main.cpp)

target_link_libraries(rf_playback_bench
    PRIVATE
        rf::gpu
        rf::media
        rf::playback
        rf::warnings)