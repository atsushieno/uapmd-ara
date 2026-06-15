cmake_minimum_required(VERSION 3.28)

project(uapmd-ara VERSION 0.1.0 LANGUAGES CXX)

if(NOT DEFINED UAPMD_REPO_ROOT)
    get_filename_component(UAPMD_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../uapmd" ABSOLUTE)
endif()

if(NOT DEFINED clap_SOURCE_DIR AND DEFINED CPM_PACKAGE_clap_SOURCE_DIR)
    set(clap_SOURCE_DIR "${CPM_PACKAGE_clap_SOURCE_DIR}")
endif()

if(NOT DEFINED vst3sdk_SOURCE_DIR AND DEFINED CPM_PACKAGE_vst3sdk_SOURCE_DIR)
    set(vst3sdk_SOURCE_DIR "${CPM_PACKAGE_vst3sdk_SOURCE_DIR}")
endif()

add_library(uapmd-ara STATIC
        src/AraSupport.cpp
        src/AraSession.cpp
        src/AraHostDocumentController.cpp
        src/AraFormatBinding.cpp
        src/AraVst3.cpp
        src/AraClap.cpp
        src/AraAudioUnit.cpp
)
add_library(uapmd::ara ALIAS uapmd-ara)

target_compile_features(uapmd-ara PUBLIC cxx_std_23)

target_include_directories(uapmd-ara
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${UAPMD_REPO_ROOT}/source/remidy/include
        ${UAPMD_REPO_ROOT}/source/uapmd/include
        ${UAPMD_REPO_ROOT}/source/uapmd-data/include
        ${UAPMD_REPO_ROOT}/source/uapmd-engine/include
        ${CMAKE_CURRENT_SOURCE_DIR}/external/ARA_SDK
        ${CMAKE_CURRENT_SOURCE_DIR}/external/ARA_SDK/ARA_API
        ${midicci_SOURCE_DIR}/include
        ${vst3sdk_SOURCE_DIR}
        ${clap_SOURCE_DIR}/include
)

target_link_libraries(uapmd-ara
        PUBLIC
        uapmd-engine
        uapmd-data
        uapmd
        remidy
)
