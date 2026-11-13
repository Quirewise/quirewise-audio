cmake_minimum_required(VERSION 3.21)
project(QuirewiseAudio VERSION 0.1.0 LANGUAGES C)
option(QUIREWISE_BUILD_SHARED "Build Quirewise libraries as shared libraries" OFF)
if(QUIREWISE_BUILD_SHARED)
    set(QW_LIBRARY_TYPE SHARED)
else()
    set(QW_LIBRARY_TYPE STATIC)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
find_package(QuirewiseCompression CONFIG REQUIRED)
find_package(QuirewiseRuntime CONFIG REQUIRED)
if(NOT TARGET Quirewise::Crypto)
    find_package(QuirewiseCrypto CONFIG REQUIRED)
endif()
file(GLOB_RECURSE QW_SOURCES CONFIGURE_DEPENDS src/*.c src/*.cpp)
add_library(quirewise_audio ${QW_LIBRARY_TYPE} ${QW_SOURCES})
add_library(Quirewise::Audio ALIAS quirewise_audio)
set_target_properties(quirewise_audio PROPERTIES EXPORT_NAME Audio POSITION_INDEPENDENT_CODE ON)
target_compile_features(quirewise_audio PUBLIC c_std_11)
target_compile_definitions(quirewise_audio PRIVATE
    QUIREWISE_PLATFORM_UNICODE_C_IMPLEMENTATION=1
    QUIREWISE_ENCRYPTED_MUSIC_PARSER_ONLY=1)
target_link_libraries(quirewise_audio
    PUBLIC Quirewise::Compression
    PRIVATE Quirewise::Crypto)
target_include_directories(quirewise_audio PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/core ${CMAKE_CURRENT_SOURCE_DIR}/src/formats ${CMAKE_CURRENT_SOURCE_DIR}/src/backends)
if(BUILD_TESTING)
    add_executable(quirewise_audio_aes_tests tests/test_aes.c)
    target_compile_features(quirewise_audio_aes_tests PRIVATE c_std_11)
    target_include_directories(quirewise_audio_aes_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/formats/audio")
    target_link_libraries(quirewise_audio_aes_tests PRIVATE
        Quirewise::Audio
        Quirewise::Crypto)
    add_test(NAME quirewise_audio_aes_known_answer
        COMMAND quirewise_audio_aes_tests)

    add_executable(quirewise_audio_encrypted_music_kgm_test
        tests/encrypted_music_kgm_test.c)
    target_compile_features(quirewise_audio_encrypted_music_kgm_test PRIVATE c_std_11)
    target_link_libraries(quirewise_audio_encrypted_music_kgm_test PRIVATE
        Quirewise::Audio
        Quirewise::Crypto
        $<$<NOT:$<PLATFORM_ID:Windows>>:m>)
    add_test(NAME quirewise_audio_encrypted_music_kgm
        COMMAND quirewise_audio_encrypted_music_kgm_test)

    add_executable(quirewise_audio_fuzz_smoke tests/audio_fuzz_smoke.c)
    target_compile_features(quirewise_audio_fuzz_smoke PRIVATE c_std_11)
    target_link_libraries(quirewise_audio_fuzz_smoke PRIVATE
        Quirewise::Audio
        Quirewise::Crypto
        $<$<NOT:$<PLATFORM_ID:Windows>>:m>)
    add_test(NAME quirewise_audio_fuzz_smoke
        COMMAND quirewise_audio_fuzz_smoke)

    add_executable(quirewise_audio_wave_pcm_test tests/wave_pcm_test.c)
    target_compile_features(quirewise_audio_wave_pcm_test PRIVATE c_std_11)
    target_link_libraries(quirewise_audio_wave_pcm_test PRIVATE
        Quirewise::Audio)
    add_test(NAME quirewise_audio_wave_pcm
        COMMAND quirewise_audio_wave_pcm_test)
endif()
install(TARGETS quirewise_audio EXPORT QuirewiseAudioTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT QuirewiseAudioTargets FILE QuirewiseAudioTargets.cmake NAMESPACE Quirewise:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseAudio")
configure_package_config_file(cmake/QuirewiseAudioConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseAudioConfig.cmake" INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseAudio")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/QuirewiseAudioConfigVersion.cmake" VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseAudioConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseAudioConfigVersion.cmake" DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseAudio")

if(QUIREWISE_BUILD_SHARED AND WIN32)
    set_target_properties(
        quirewise_audio
        PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
target_link_libraries(quirewise_audio PUBLIC Quirewise::Runtime)
