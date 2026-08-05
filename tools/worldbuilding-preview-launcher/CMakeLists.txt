cmake_minimum_required(VERSION 3.16)
project(gplates-worldbuilding-preview-launcher LANGUAGES CXX)

add_executable(gplates-worldbuilding-preview-launcher WIN32
    WorldbuildingPreviewLauncher.cc)
target_compile_features(gplates-worldbuilding-preview-launcher PRIVATE cxx_std_17)
target_link_libraries(gplates-worldbuilding-preview-launcher PRIVATE shell32)

if(MINGW)
    target_link_options(gplates-worldbuilding-preview-launcher PRIVATE
        -static-libgcc -static-libstdc++)
endif()
