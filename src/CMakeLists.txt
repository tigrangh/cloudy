if( WIN32 )
    SET(CLOUDY_INSTALL_DESTINATION_RUNTIME bin)
    SET(CLOUDY_INSTALL_DESTINATION_LIBRARY lib)
    SET(CLOUDY_INSTALL_DESTINATION_ARCHIVE lib)
    SET(CLOUDY_INSTALL_DESTINATION_INCLUDE include)
else( WIN32 )
    SET(CLOUDY_INSTALL_DESTINATION_RUNTIME bin/cloudy)
    SET(CLOUDY_INSTALL_DESTINATION_LIBRARY lib/cloudy)
    SET(CLOUDY_INSTALL_DESTINATION_ARCHIVE lib/cloudy)
    SET(CLOUDY_INSTALL_DESTINATION_INCLUDE include/cloudy)
endif( WIN32 )

if( NOT WIN32 )
    # don't skip the full RPATH for the build tree
    SET(CMAKE_SKIP_BUILD_RPATH  FALSE)

    # when building, don't use the install RPATH already
    # (but later on when installing)
    SET(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

    SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib/cloudy;CMAKE_INSTALL_RPATH;${CMAKE_INSTALL_PREFIX}/lib/belt.pp;${CMAKE_INSTALL_PREFIX}/lib/mesh.pp")

endif()

include(cmake_utility/utility.cmake)

if(NOT TARGET belt.pp)
add_subdirectory(belt.pp)
endif()
if(NOT TARGET mesh.pp)
add_subdirectory(mesh.pp)
endif()
find_package(belt.pp)
find_package(mesh.pp)

add_subdirectory(cloudy)
add_subdirectory(cloudyd)
add_subdirectory(libcloudyserver)

# following is used for find_package functionality
install(FILES cloudy-config.cmake DESTINATION ${CLOUDY_INSTALL_DESTINATION_LIBRARY})
install(EXPORT cloudy.package NAMESPACE cloudy:: DESTINATION ${CLOUDY_INSTALL_DESTINATION_LIBRARY})
