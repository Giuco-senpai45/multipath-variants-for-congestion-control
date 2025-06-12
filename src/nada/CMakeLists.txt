set(source_files
  nada-improved.cc
  nada-header.cc
  nada-udp-client.cc
  mp-nada-client.cc
  video-receiver.cc
  agg-path-nada.cc
  mp-nada/mp-best.cc
  mp-nada/mp-buffer.cc
  mp-nada/mp-factory.cc
  mp-nada/mp-frame.cc
  mp-nada/mp-nada-base.cc
  mp-nada/mp-rr.cc
  mp-nada/mp-weighted.cc
)

set(header_files
  nada-improved.h
  nada-header.h
  nada-udp-client.h
  mp-nada-client.h
  video-receiver.h
  agg-path-nada.h
  mp-nada/mp-best.h
  mp-nada/mp-buffer.h
  mp-nada/mp-factory.h
  mp-nada/mp-frame.h
  mp-nada/mp-nada-base.h
  mp-nada/mp-rr.h
  mp-nada/mp-weighted.h
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
