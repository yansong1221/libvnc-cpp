SET(MOUDLE libvnc-cpp)

find_package(Boost REQUIRED COMPONENTS asio endian)
find_package(spdlog CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

include(FetchContent)
FetchContent_Declare(ZStrGitRepo
  GIT_REPOSITORY    "https://github.com/mateidavid/zstr" # can also be a local filesystem path!
  GIT_TAG           "master"
)
FetchContent_MakeAvailable(ZStrGitRepo) # defines INTERFACE target 'zstr::zstr'

find_package(ZLIB REQUIRED)


file(GLOB_RECURSE MOUDLE_SOURCE_FILES  ${LIBVNC_CPP_SRC_DIR}/*.h ${LIBVNC_CPP_SRC_DIR}/*.c ${LIBVNC_CPP_SRC_DIR}/*.cxx ${LIBVNC_CPP_SRC_DIR}/*.cpp ${LIBVNC_CPP_SRC_DIR}/*.hpp)
source_group(TREE ${LIBVNC_CPP_SRC_DIR} PREFIX "src" FILES ${MOUDLE_SOURCE_FILES})


file(GLOB_RECURSE MOUDLE_HEADER_FILES "${LIBVNC_CPP_INCLUDE_DIR}/*.h" "${LIBVNC_CPP_INCLUDE_DIR}/*.hpp" "${LIBVNC_CPP_INCLUDE_DIR}/*.inl")
source_group(TREE ${LIBVNC_CPP_INCLUDE_DIR} PREFIX "include" FILES ${MOUDLE_HEADER_FILES})

add_library(${MOUDLE} ${MOUDLE_SOURCE_FILES} ${MOUDLE_HEADER_FILES})

target_link_libraries(${MOUDLE} PUBLIC Boost::asio Boost::endian
 spdlog::spdlog   
 OpenSSL::SSL OpenSSL::Crypto
PRIVATE zstr::zstr  ZLIB::ZLIB
)

target_include_directories(${MOUDLE}
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBVNC_CPP_INCLUDE_DIR}
)
target_compile_definitions(${MOUDLE} PRIVATE LIBVNC_HAVE_LIBZ)
