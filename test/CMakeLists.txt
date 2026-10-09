
add_library(munit "${CMAKE_CURRENT_SOURCE_DIR}/munit/munit.c")
target_include_directories(munit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/munit")

# MinGW's tmpfile() fails in some Windows/MSYS environments. MUnit uses it
# only to buffer test stderr, so disable that optional capture on Windows.
if(WIN32)
	target_compile_definitions(munit PRIVATE MUNIT_NO_BUFFER)
endif()

set(CHIAKI_UNIT_SOURCES
				main.c
				http.c
				rpcrypt.c
				gkcrypt.c
				takion.c
				seqnum.c
				keystate.c
				feedback.c
				reorderqueue.c
				fec.c
				test_log.c
				test_log.h
				bitstream.c
				regist.c)

if(NOT CHIAKI_LIB_ENABLE_MBEDTLS)
	list(APPEND CHIAKI_UNIT_SOURCES aia.c)
endif()

if(CHIAKI_ENABLE_FFMPEG_DECODER)
	list(APPEND CHIAKI_UNIT_SOURCES ffmpegdecoder.c)
endif()

add_executable(chiaki-unit ${CHIAKI_UNIT_SOURCES})
add_test(NAME unit COMMAND chiaki-unit)

target_link_libraries(chiaki-unit chiaki-lib munit)

if(CHIAKI_ENABLE_FFMPEG_DECODER)
	target_link_libraries(chiaki-unit FFMPEG::avcodec FFMPEG::avutil)
endif()
