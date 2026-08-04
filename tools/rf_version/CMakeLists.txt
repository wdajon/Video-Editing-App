add_executable(rf_version main.cpp)

target_link_libraries(rf_version
    PRIVATE
        rf::core
        rf::warnings)
