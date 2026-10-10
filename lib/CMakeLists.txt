
set(HEADER_FILES
		include/chiaki/session.h
		include/chiaki/common.h
		include/chiaki/sock.h
		include/chiaki/thread.h
		include/chiaki/base64.h
		include/chiaki/http.h
		include/chiaki/log.h
		include/chiaki/ctrl.h
		include/chiaki/rpcrypt.h
		include/chiaki/takion.h
		include/chiaki/senkusha.h
		include/chiaki/streamconnection.h
		include/chiaki/ecdh.h
		include/chiaki/launchspec.h
		include/chiaki/random.h
		include/chiaki/gkcrypt.h
		include/chiaki/audio.h
		include/chiaki/audioreceiver.h
		include/chiaki/audiosender.h
		include/chiaki/video.h
		include/chiaki/videoreceiver.h
		include/chiaki/frameprocessor.h
		include/chiaki/packetstats.h
		include/chiaki/seqnum.h
		include/chiaki/discovery.h
		include/chiaki/congestioncontrol.h
		include/chiaki/stoppipe.h
		include/chiaki/reorderqueue.h
		include/chiaki/discoveryservice.h
		include/chiaki/feedback.h
		include/chiaki/feedbacksender.h
		include/chiaki/controller.h
		include/chiaki/takionsendbuffer.h
		include/chiaki/time.h
		include/chiaki/fec.h
		include/chiaki/regist.h
		include/chiaki/opusdecoder.h
		include/chiaki/opusencoder.h
		include/chiaki/orientation.h
		include/chiaki/bitstream.h
		include/chiaki/remote/holepunch.h
		include/chiaki/remote/rudp.h
		include/chiaki/remote/rudpsendbuffer.h)

set(SOURCE_FILES
		src/common.c
		src/sock.c
		src/session.c
		src/thread.c
		src/base64.c
		src/http.c
		src/log.c
		src/ctrl.c
		src/rpcrypt.c
		src/takion.c
		src/senkusha.c
		src/utils.h
		src/pb_utils.h
		src/streamconnection.c
		src/ecdh.c
		src/launchspec.c
		src/random.c
		src/gkcrypt.c
		src/audio.c
		src/audioreceiver.c
		src/audiosender.c
		src/videoreceiver.c
		src/frameprocessor.c
		src/packetstats.c
		src/discovery.c
		src/congestioncontrol.c
		src/stoppipe.c
		src/reorderqueue.c
		src/discoveryservice.c
		src/feedback.c
		src/feedbacksender.c
		src/controller.c
		src/takionsendbuffer.c
		src/time.c
		src/fec.c
		src/regist.c
		src/opusdecoder.c
		src/opusencoder.c
		src/orientation.c
		src/gyrosteer.c
		src/bitstream.c
		src/aia.c
		src/aia.h
		src/remote/holepunch.c
		src/remote/rudp.c
		src/remote/rudpsendbuffer.c)

if(NOT CHIAKI_LIB_ENABLE_MBEDTLS)
	list(APPEND SOURCE_FILES src/aia_openssl.c)
endif()

if(CHIAKI_ENABLE_FFMPEG_DECODER)
	list(APPEND HEADER_FILES include/chiaki/ffmpegdecoder.h)
	list(APPEND SOURCE_FILES src/ffmpegdecoder.c)
endif()
set(CHIAKI_LIB_ENABLE_FFMPEG_DECODER "${CHIAKI_ENABLE_FFMPEG_DECODER}")
set(CHIAKI_LIB_ENABLE_PI_DECODER "${CHIAKI_ENABLE_FFMPEG_DECODER}")

if(CHIAKI_ENABLE_PI_DECODER)
	list(APPEND HEADER_FILES include/chiaki/pidecoder.h)
	list(APPEND SOURCE_FILES src/pidecoder.c)
endif()
set(CHIAKI_LIB_ENABLE_PI_DECODER "${CHIAKI_ENABLE_PI_DECODER}")

add_subdirectory(protobuf)
set_source_files_properties(${CHIAKI_LIB_PROTO_SOURCE_FILES} ${CHIAKI_LIB_PROTO_HEADER_FILES} PROPERTIES GENERATED TRUE)
include_directories("${CHIAKI_LIB_PROTO_INCLUDE_DIR}")

if(CHIAKI_LIB_ENABLE_OPUS)
	find_package(Opus REQUIRED)
	include_directories(${Opus_INCLUDE_DIRS})
endif()

add_library(chiaki-lib ${HEADER_FILES} ${SOURCE_FILES} ${CHIAKI_LIB_PROTO_SOURCE_FILES} ${CHIAKI_LIB_PROTO_HEADER_FILES})
if(WIN32 AND NOT CHIAKI_USE_SYSTEM_CURL AND NOT CHIAKI_LIB_ENABLE_MBEDTLS)
	target_compile_definitions(chiaki-lib PRIVATE CHIAKI_CURL_SCHANNEL_AIA=1)
endif()
configure_file(config.h.in include/chiaki/config.h)
target_include_directories(chiaki-lib PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/include")

if(CMAKE_C_COMPILER_ID STREQUAL GNU OR CMAKE_C_COMPILER_ID STREQUAL Clang)
	target_compile_options(chiaki-lib PRIVATE -Wall)
endif()

add_dependencies(chiaki-lib chiaki-pb)
set_target_properties(chiaki-lib PROPERTIES OUTPUT_NAME chiaki)

if(WIN32)
	target_link_libraries(chiaki-lib wsock32 ws2_32 bcrypt iphlpapi)
	if(NOT CHIAKI_LIB_ENABLE_MBEDTLS)
		target_link_libraries(chiaki-lib crypt32)
	endif()
endif()

if(APPLE AND NOT IOS AND NOT TVOS)
	target_link_libraries(chiaki-lib "-framework CoreServices")
endif()

target_include_directories(chiaki-lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

find_package(Threads REQUIRED)
target_link_libraries(chiaki-lib Threads::Threads)

if(CHIAKI_LIB_JSONC_EXTERNAL_PROJECT)
	target_link_libraries(chiaki-lib json-c)
	target_include_directories(chiaki-lib PRIVATE "${JSONC_PREFIX_INCLUDE_DIR}")
	target_include_directories(chiaki-lib PRIVATE "${json-c_BINARY_DIR}")
elseif(CHIAKI_IS_SWITCH)
	find_library(JSONC_LIB json-c ${PORTLIBS}/lib)
	target_link_libraries(chiaki-lib ${JSONC_LIB})
	target_include_directories(chiaki-lib PUBLIC "${PORTLIBS}/include/json-c")
else()
	find_package(PkgConfig REQUIRED)
	pkg_search_module(json-c REQUIRED json-c IMPORTED_TARGET)
	target_include_directories(chiaki-lib PRIVATE ${json-c_INCLUDE_DIRS})
	target_link_libraries(chiaki-lib PkgConfig::json-c)
endif()

if(CHIAKI_LIB_MINIUPNPC_EXTERNAL_PROJECT)
	target_link_libraries(chiaki-lib libminiupnpc-static)
	target_include_directories(chiaki-lib PRIVATE "${MINIUPNPC_PREFIX_INCLUDE_DIR}")
else()
	find_package(PkgConfig REQUIRED)
	pkg_search_module(MINIUPNPC REQUIRED miniupnpc IMPORTED_TARGET)
	target_link_libraries(chiaki-lib PkgConfig::MINIUPNPC)
endif()

if(NOT CHIAKI_IS_SWITCH AND NOT CHIAKI_ENABLE_ANDROID)
	find_program(PKG_CONFIG_EXECUTABLE pkg-config)
	if(PKG_CONFIG_EXECUTABLE)
		pkg_search_module(LIBEVENT libevent QUIET IMPORTED_TARGET)
	endif()
	if(TARGET PkgConfig::LIBEVENT)
		target_link_libraries(chiaki-lib PkgConfig::LIBEVENT)
	else()
		find_package(libevent REQUIRED)
		target_include_directories(chiaki-lib PRIVATE ${LIBEVENT_INCLUDE_DIRS})
		target_link_libraries(chiaki-lib ${LIBEVENT_LIBRARIES})
	endif()
endif()

target_link_libraries(chiaki-lib CURL::libcurl)

if(CHIAKI_LIB_ENABLE_MBEDTLS)
	if(CHIAKI_LIB_MBEDTLS_EXTERNAL_PROJECT)
		target_link_libraries(chiaki-lib mbedtls mbedx509 mbedcrypto)
	else()
		# provided by mbedtls-static (mbedtls-devel)
		find_library(MBEDTLS mbedtls)
		find_library(MBEDX509 mbedx509)
		find_library(MBEDCRYPTO mbedcrypto)
		target_link_libraries(chiaki-lib ${MBEDTLS} ${MBEDX509} ${MBEDCRYPTO})
	endif()
elseif(CHIAKI_LIB_OPENSSL_EXTERNAL_PROJECT)
	target_link_libraries(chiaki-lib OpenSSL_Crypto)
else()
	# default
	find_package(OpenSSL REQUIRED)
	target_link_libraries(chiaki-lib OpenSSL::Crypto)
endif()

target_link_libraries(chiaki-lib Nanopb::nanopb)
target_link_libraries(chiaki-lib Jerasure::Jerasure)

if(CHIAKI_ENABLE_FFMPEG_DECODER)
	target_link_libraries(chiaki-lib FFMPEG::avcodec FFMPEG::avutil)
endif()

if(CHIAKI_ENABLE_PI_DECODER)
	target_link_libraries(chiaki-lib ILClient::ILClient)
endif()

if(CHIAKI_LIB_ENABLE_OPUS)
	target_link_libraries(chiaki-lib ${Opus_LIBRARIES})
endif()

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fanalyzer")
if(CHIAKI_ENABLE_TESTS)
	add_executable(holepunch-test include/chiaki/remote/holepunch.h src/remote/holepunch-test.c)
	target_link_libraries(holepunch-test chiaki-lib)
endif()
