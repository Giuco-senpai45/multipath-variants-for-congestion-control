set(source_files
  nada-improved.cc
  nada-header.cc
  nada-udp-client.cc
  mp-nada-client.cc
)

set(header_files
  nada-improved.h
  nada-header.h
  nada-udp-client.h
  mp-nada-client.h
)

build_lib(
  LIBNAME nada
  SOURCE_FILES ${source_files}
  HEADER_FILES ${header_files}
  LIBRARIES_TO_LINK ${libcore}
                    ${libnetwork}
                    ${libinternet}
                    ${libapplications}
  TEST_SOURCES
)
