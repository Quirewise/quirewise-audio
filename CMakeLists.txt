cmake_minimum_required(VERSION 3.21)
project(QuirewiseAudio VERSION 0.1.0 LANGUAGES C)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
file(GLOB_RECURSE QW_SOURCES CONFIGURE_DEPENDS src/*.c src/*.cpp)
add_library(quirewise_audio STATIC ${QW_SOURCES})
add_library(Quirewise::Audio ALIAS quirewise_audio)
set_target_properties(quirewise_audio PROPERTIES EXPORT_NAME Audio POSITION_INDEPENDENT_CODE ON)
target_compile_features(quirewise_audio PUBLIC c_std_11)
target_include_directories(quirewise_audio PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/core ${CMAKE_CURRENT_SOURCE_DIR}/src/formats ${CMAKE_CURRENT_SOURCE_DIR}/src/backends ${CMAKE_CURRENT_SOURCE_DIR}/third_party/aes)
install(TARGETS quirewise_audio EXPORT QuirewiseAudioTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT QuirewiseAudioTargets FILE QuirewiseAudioTargets.cmake NAMESPACE Quirewise:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseAudio")
configure_package_config_file(cmake/QuirewiseAudioConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseAudioConfig.cmake" INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseAudio")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/QuirewiseAudioConfigVersion.cmake" VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseAudioConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseAudioConfigVersion.cmake" DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseAudio")

