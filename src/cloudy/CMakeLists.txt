# interface library for headers only module
add_library(cloudy INTERFACE)

# modules linking to this library will include following
# directories. two options are provided for installed package
# and for in-tree source build
target_include_directories(cloudy INTERFACE
    $<INSTALL_INTERFACE:include/cloudy>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)

# libraries this module links to
target_link_libraries(cloudy INTERFACE belt.pp)

# what to do on make install
install(TARGETS cloudy
        EXPORT cloudy.package
        DESTINATION ${CLOUDY_INSTALL_DESTINATION_LIBRARY})

set(SRC_FILES
    global.hpp
    admin_server.hpp
    direct_stream.hpp
    storage_server.hpp
    worker.hpp)

install(FILES
    ${SRC_FILES}
    DESTINATION ${CLOUDY_INSTALL_DESTINATION_INCLUDE}/cloudy)

#
# add custom target simply for IDE to show the files
#
add_custom_target(cloudy.include SOURCES
    ${SRC_FILES})
