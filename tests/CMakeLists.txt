function(bcs_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE bcs_core)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300)
endfunction()

bcs_add_test(test_quality_mapping)
bcs_add_test(test_buffers)
bcs_add_test(test_video_roundtrip)
bcs_add_test(test_audio_roundtrip)
bcs_add_test(test_video_pipeline)
bcs_add_test(test_loop_clip)

# Needs an audio server (PipeWire/PulseAudio/WASAPI); exits 77 = skipped otherwise.
bcs_add_test(test_audio_pipeline)
set_tests_properties(test_audio_pipeline PROPERTIES SKIP_RETURN_CODE 77)

if(BCS_LINUX)
    bcs_add_test(test_loopback_setup)
    target_compile_definitions(test_loopback_setup PRIVATE
        BCS_HELPER_SCRIPT="${PROJECT_SOURCE_DIR}/packaging/linux/bcs-loopback-helper.sh")
endif()
