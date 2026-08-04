add_executable(rf_seek_check main.cpp)

target_link_libraries(rf_seek_check
    PRIVATE
        rf::media
        rf::warnings)
