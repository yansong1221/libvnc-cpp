SET(MOUDLE libvnc-cpp)



find_package(Boost REQUIRED COMPONENTS asio endian)
find_package(spdlog CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(botan CONFIG REQUIRED)

include(FetchContent)
FetchContent_Declare(ZStrGitRepo
  GIT_REPOSITORY    "https://github.com/mateidavid/zstr.git" # can also be a local filesystem path!
  GIT_TAG           "master"
)
FetchContent_MakeAvailable(ZStrGitRepo) # defines INTERFACE target 'zstr::zstr'


find_package(PkgConfig REQUIRED)
pkg_check_modules(LZO REQUIRED lzo2)

find_package(JPEG REQUIRED)
find_package(libjpeg-turbo CONFIG REQUIRED)


file(GLOB_RECURSE MOUDLE_SOURCE_FILES  ${LIBVNC_CPP_SRC_DIR}/*.h ${LIBVNC_CPP_SRC_DIR}/*.c ${LIBVNC_CPP_SRC_DIR}/*.cxx ${LIBVNC_CPP_SRC_DIR}/*.cpp ${LIBVNC_CPP_SRC_DIR}/*.hpp)
source_group(TREE ${LIBVNC_CPP_SRC_DIR} PREFIX "src" FILES ${MOUDLE_SOURCE_FILES})


file(GLOB_RECURSE MOUDLE_HEADER_FILES "${LIBVNC_CPP_INCLUDE_DIR}/*.h" "${LIBVNC_CPP_INCLUDE_DIR}/*.hpp" "${LIBVNC_CPP_INCLUDE_DIR}/*.inl")
source_group(TREE ${LIBVNC_CPP_INCLUDE_DIR} PREFIX "include" FILES ${MOUDLE_HEADER_FILES})

add_library(${MOUDLE} ${MOUDLE_SOURCE_FILES} ${MOUDLE_HEADER_FILES})

target_link_libraries(${MOUDLE} PUBLIC Boost::asio Boost::endian
OpenSSL::SSL OpenSSL::Crypto
spdlog::spdlog   
zstr::zstr  ZLIB::ZLIB
JPEG::JPEG
$<IF:$<TARGET_EXISTS:Botan::Botan>,Botan::Botan,Botan::Botan-static>
$<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>
)

if(LZO_FOUND)
    message(STATUS "Found LZO via pkg-config")
    target_include_directories(${MOUDLE} PRIVATE ${LZO_INCLUDE_DIRS})
    target_link_directories(${MOUDLE} PUBLIC ${LZO_LIBRARY_DIRS})
    target_link_libraries(${MOUDLE} PUBLIC ${LZO_LIBRARIES})
    
else()
    message(FATAL_ERROR "LZO library not found")
endif()

target_include_directories(${MOUDLE}
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBVNC_CPP_INCLUDE_DIR}
)
target_compile_definitions(${MOUDLE} PRIVATE LIBVNC_HAVE_LIBZ)

if (WIN32)
    include (win32_version.cmake)

    get_WIN32_WINNT(winnt_version)

    target_compile_definitions(
        ${MOUDLE}
        PUBLIC
            _WIN32_WINNT=${winnt_version}   # Keep this public so user's can't accidentally use conflicting versions
    )
endif()
