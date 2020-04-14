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
    common.cpp
    common.hpp
    direct_stream.cpp
    direct_stream.hpp
    libavwrapper.cpp
    libavwrapper.hpp
    internal_model.hpp
    internal_model.gen.hpp
    library.cpp
    library.hpp
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

# find ffmpeg libraries
find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h)
find_library(AVCODEC_LIBRARY avcodec)

add_library( avcodec UNKNOWN IMPORTED )
set_property( TARGET avcodec PROPERTY IMPORTED_LOCATION "${AVCODEC_LIBRARY}" )
set_property( TARGET avcodec PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${AVCODEC_INCLUDE_DIR}" )

find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h)
find_library(AVFORMAT_LIBRARY avformat)

add_library( avformat UNKNOWN IMPORTED )
set_property( TARGET avformat PROPERTY IMPORTED_LOCATION "${AVFORMAT_LIBRARY}" )
set_property( TARGET avformat PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${AVFORMAT_INCLUDE_DIR}" )

# comment later?
find_path(AVFILTER_INCLUDE_DIR libavfilter/avfilter.h)
find_library(AVFILTER_LIBRARY avfilter)

add_library( avfilter UNKNOWN IMPORTED )
set_property( TARGET avfilter PROPERTY IMPORTED_LOCATION "${AVFILTER_LIBRARY}" )
set_property( TARGET avfilter PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${AVFILTER_INCLUDE_DIR}" )

#find_path(POSTPROC_INCLUDE_DIR libpostproc/postprocess.h)
#find_library(POSTPROC_LIBRARY postproc)

#add_library( postproc UNKNOWN IMPORTED )
#set_property( TARGET postproc PROPERTY IMPORTED_LOCATION "${POSTPROC_LIBRARY}" )
#set_property( TARGET postproc PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${POSTPROC_INCLUDE_DIR}" )

#find_path(AVDEVICE_INCLUDE_DIR libavdevice/avdevice.h)
#find_library(AVDEVICE_LIBRARY avdevice)

#add_library( avdevice UNKNOWN IMPORTED )
#set_property( TARGET avdevice PROPERTY IMPORTED_LOCATION "${AVDEVICE_LIBRARY}" )
#set_property( TARGET avdevice PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${AVDEVICE_INCLUDE_DIR}" )

#find_path(AVRESAMPLE_INCLUDE_DIR libavresample/avresample.h)
#find_library(AVRESAMPLE_LIBRARY avresample)

#add_library( avresample UNKNOWN IMPORTED )
#set_property( TARGET avresample PROPERTY IMPORTED_LOCATION "${AVRESAMPLE_LIBRARY}" )
#set_property( TARGET avresample PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${AVRESAMPLE_INCLUDE_DIR}" )

#find_path(SWRESAMPLE_INCLUDE_DIR libswresample/swresample.h)
#find_library(SWRESAMPLE_LIBRARY swresample)

#add_library( swresample UNKNOWN IMPORTED )
#set_property( TARGET swresample PROPERTY IMPORTED_LOCATION "${SWRESAMPLE_LIBRARY}" )
#set_property( TARGET swresample PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${SWRESAMPLE_INCLUDE_DIR}" )

#find_path(SWSCALE_INCLUDE_DIR libswscale/swscale.h)
#find_library(SWSCALE_LIBRARY swscale)

#add_library( swscale UNKNOWN IMPORTED )
#set_property( TARGET swscale PROPERTY IMPORTED_LOCATION "${SWSCALE_LIBRARY}" )
#set_property( TARGET swscale PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${SWSCALE_INCLUDE_DIR}" )

find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h)
find_library(AVUTIL_LIBRARY avutil)

add_library( avutil UNKNOWN IMPORTED )
set_property( TARGET avutil PROPERTY IMPORTED_LOCATION "${AVUTIL_LIBRARY}" )
set_property( TARGET avutil PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${AVUTIL_INCLUDE_DIR}" )

MESSAGE(STATUS "ffmpeg info")
MESSAGE(STATUS "${AVCODEC_LIBRARY}")
MESSAGE(STATUS "${AVCODEC_INCLUDE_DIR}")
MESSAGE(STATUS "${AVFORMAT_LIBRARY}")
MESSAGE(STATUS "${AVFORMAT_INCLUDE_DIR}")
MESSAGE(STATUS "${AVFILTER_LIBRARY}")
MESSAGE(STATUS "${AVFILTER_INCLUDE_DIR}")
#MESSAGE(STATUS "${POSTPROC_LIBRARY}")
#MESSAGE(STATUS "${POSTPROC_INCLUDE_DIR}")
#MESSAGE(STATUS "${AVDEVICE_LIBRARY}")
#MESSAGE(STATUS "${AVDEVICE_INCLUDE_DIR}")
#MESSAGE(STATUS "${AVRESAMPLE_LIBRARY}")
#MESSAGE(STATUS "${AVRESAMPLE_INCLUDE_DIR}")
#MESSAGE(STATUS "${SWRESAMPLE_LIBRARY}")
#MESSAGE(STATUS "${SWRESAMPLE_INCLUDE_DIR}")
#MESSAGE(STATUS "${SWSCALE_LIBRARY}")
#MESSAGE(STATUS "${SWSCALE_INCLUDE_DIR}")
MESSAGE(STATUS "${AVUTIL_LIBRARY}")
MESSAGE(STATUS "${AVUTIL_INCLUDE_DIR}")

target_link_libraries(cloudyserver
    PRIVATE
        avcodec
        avformat
        avfilter
        ###postproc
        #avdevice
        ###avresample
        ###swresample
        ###swscale
        avutil)

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
