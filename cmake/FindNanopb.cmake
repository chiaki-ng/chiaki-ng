# Provides Nanopb::nanopb and NANOPB_GENERATOR_PY

find_package(nanopb CONFIG)
find_file(NANOPB_GENERATOR_PY nanopb_generator.py PATH_SUFFIXES bin)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Nanopb
	FOUND_VAR Nanopb_FOUND
	REQUIRED_VARS
		nanopb_FOUND
		NANOPB_GENERATOR_PY
)

if(Nanopb_FOUND)
	if(NOT TARGET Nanopb::nanopb)
		if(TARGET nanopb::protobuf-nanopb)
			add_library(Nanopb::nanopb ALIAS nanopb::protobuf-nanopb)
		else()
			add_library(Nanopb::nanopb ALIAS nanopb::protobuf-nanopb-static)
		endif()
	endif()
endif()
