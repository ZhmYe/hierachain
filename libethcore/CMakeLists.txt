file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(ethcore ${SRC_LIST} ${HEADERS})
target_compile_options(ethcore PRIVATE -Wno-unused-parameter)
target_link_libraries(ethcore PUBLIC devcrypto TBB)