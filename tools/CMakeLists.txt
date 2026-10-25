# Milestone 1 prototype: codec round-trip measurement with synthetic material
# or a real camera, without the UI.
add_executable(bcs-roundtrip-bench roundtrip_bench.cpp)
target_link_libraries(bcs-roundtrip-bench PRIVATE bcs_core)
