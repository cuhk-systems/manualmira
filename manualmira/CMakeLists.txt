list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

find_package(rdmacm REQUIRED)
find_package(ibverbs REQUIRED)

add_library(manualmira STATIC
    src/rdma.cpp
)
target_include_directories(manualmira
    PUBLIC
        include
        ${IBVERBS_INCLUDE_DIRS}
    PRIVATE
        src
)
target_link_libraries(manualmira ${IBVERBS_LIBRARIES} RDMA::RDMAcm)
