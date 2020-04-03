# define the executable
add_executable(cloudyd
    main.cpp)

# libraries this module links to
target_link_libraries(cloudyd PRIVATE
    cloudy
    cloudyserver
    mesh.pp
    systemutility
    belt.pp
    cryptoutility
    log
    mlog
    Boost::filesystem
    Boost::program_options
    Boost::locale
    )

if(NOT WIN32 AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(cloudyd PRIVATE Threads::Threads)
endif()

# what to do on make install
install(TARGETS cloudyd
        EXPORT cloudy.package
        RUNTIME DESTINATION ${CLOUDY_INSTALL_DESTINATION_RUNTIME}
        LIBRARY DESTINATION ${CLOUDY_INSTALL_DESTINATION_LIBRARY}
        ARCHIVE DESTINATION ${CLOUDY_INSTALL_DESTINATION_ARCHIVE})
