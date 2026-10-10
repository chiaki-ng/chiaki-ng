# chiaki-web bridge: headless frontend relaying sessions to browsers over WebRTC

find_package(LibDataChannel QUIET)
if(NOT LibDataChannel_FOUND)
	find_library(DATACHANNEL_LIB datachannel REQUIRED)
	add_library(LibDataChannel::LibDataChannel SHARED IMPORTED)
	set_target_properties(LibDataChannel::LibDataChannel PROPERTIES IMPORTED_LOCATION "${DATACHANNEL_LIB}")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS_PC REQUIRED IMPORTED_TARGET opus)

# Header-only dependencies, fetched and pinned at configure time rather than
# vendored into the tree:
#   nlohmann/json — ergonomic JSON for the REST + signaling layer
#   cpp-httplib   — HTTP server (static frontend + REST) and the HTTPS client
#                   used to mint short-lived Cloudflare TURN credentials
include(FetchContent)

set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
	GIT_REPOSITORY https://github.com/nlohmann/json.git
	GIT_TAG v3.11.3
	GIT_SHALLOW ON)

# HTTPS is required for the TURN credential-minting client; fail loudly if the
# TLS backend is missing rather than silently dropping to http-only.
set(HTTPLIB_REQUIRE_OPENSSL ON CACHE INTERNAL "")
set(HTTPLIB_INSTALL OFF CACHE INTERNAL "")
FetchContent_Declare(cpp-httplib
	GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
	GIT_TAG v0.18.3
	GIT_SHALLOW ON)

FetchContent_MakeAvailable(nlohmann_json cpp-httplib)

add_executable(chiaki-webbridge
	src/main.cpp
	src/config.cpp
	src/chiakisource.cpp
	src/demosource.cpp
	src/relay.cpp
	src/wstransport.cpp
	src/discoverymgr.cpp
	src/registration.cpp
	src/server.cpp)

set_target_properties(chiaki-webbridge PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON)

# httplib::httplib carries CPPHTTPLIB_OPENSSL_SUPPORT and links OpenSSL
# transitively (HTTPLIB_REQUIRE_OPENSSL above).
target_link_libraries(chiaki-webbridge
	chiaki-lib
	LibDataChannel::LibDataChannel
	PkgConfig::OPUS_PC
	nlohmann_json::nlohmann_json
	httplib::httplib
	Threads::Threads)

# Default frontend location next to the binary for `ninja && run` workflows.
# Symlink (not copy) so edits to the frontend sources are always live.
add_custom_command(TARGET chiaki-webbridge POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:chiaki-webbridge>/frontend"
	COMMAND ${CMAKE_COMMAND} -E create_symlink
	"${CMAKE_CURRENT_SOURCE_DIR}/frontend" "$<TARGET_FILE_DIR:chiaki-webbridge>/frontend")
