# generate admin_model.gen.hpp from admin_model.idl
add_custom_command (
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/admin_model.gen.hpp
  MAIN_DEPENDENCY admin_model.idl
  COMMAND idl ${CMAKE_CURRENT_SOURCE_DIR}/admin_model.idl ${CMAKE_CURRENT_SOURCE_DIR}/admin_model.gen
)
# generate storage_model.gen.hpp from storage_model.idl
add_custom_command (
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/storage_model.gen.hpp
  MAIN_DEPENDENCY storage_model.idl
  COMMAND idl ${CMAKE_CURRENT_SOURCE_DIR}/storage_model.idl ${CMAKE_CURRENT_SOURCE_DIR}/storage_model.gen
)
# generate internal_model.gen.hpp from internal_model.idl
add_custom_command (
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/internal_model.gen.hpp
  MAIN_DEPENDENCY internal_model.idl
  COMMAND idl ${CMAKE_CURRENT_SOURCE_DIR}/internal_model.idl ${CMAKE_CURRENT_SOURCE_DIR}/internal_model.gen
)

add_definitions(-DCLOUDYSERVER_LIBRARY)

# define the library
add_library(cloudyserver
    global.hpp
    admin_model.hpp
    admin_model.gen.hpp
    admin_http.hpp
    admin_server.cpp
    admin_server.hpp
    catalogue_tree.cpp
    catalogue_tree.hpp
    common.cpp
    common.hpp
    direct_stream.cpp
    direct_stream.hpp
    internal_model.hpp
    internal_model.gen.hpp
    storage.cpp
    storage.hpp
    storage_model.hpp
    storage_model.gen.hpp
    storage_http.hpp
    storage_server.cpp
    storage_server.hpp
    worker.cpp
    worker.hpp)

# libraries this module links to
target_link_libraries(cloudyserver
    PUBLIC
        cloudy
    INTERFACE
        belt.pp
    PRIVATE
        cryptoutility
        packet
        socket
        processor
        utility
        Boost::filesystem)

# what to do on make install
install(TARGETS cloudyserver
        EXPORT cloudy.package
        RUNTIME DESTINATION ${CLOUDY_INSTALL_DESTINATION_RUNTIME}
        LIBRARY DESTINATION ${CLOUDY_INSTALL_DESTINATION_LIBRARY}
        ARCHIVE DESTINATION ${CLOUDY_INSTALL_DESTINATION_ARCHIVE})

install(FILES
    global.hpp
    admin_server.hpp
    storage_server.hpp
    worker.hpp
    direct_stream.hpp
    DESTINATION ${CLOUDY_INSTALL_DESTINATION_INCLUDE}/liblog)
