# Measures the per-frame cost of rendering a timeline through the readback path
# the Program panel uses, so the decision to replace it is made on a number
# rather than on the assumption that readback must be slow.

add_executable(rf_render_bench main.cpp)

target_link_libraries(rf_render_bench
    PRIVATE
        rf::render
        rf::media
        rf::warnings)
