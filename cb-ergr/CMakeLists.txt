# CB-ERGR v5 module (contrib/cb-ergr). Depends on aqua-sim-ng.
# Sources are added milestone by milestone (see doc/ and the spec, section 15).
build_lib(
    LIBNAME cb-ergr
    SOURCE_FILES
        model/cb-ergr-config.cc
    HEADER_FILES
        model/cb-ergr-config.h
    LIBRARIES_TO_LINK
        ${libaqua-sim-ng}
        ${libmobility}
        ${libenergy}
        ${libnetwork}
        ${libcore}
    TEST_SOURCES
        test/cb-ergr-test-suite.cc
)

build_lib_example(
    NAME cb-ergr-smoke
    SOURCE_FILES examples/cb-ergr-smoke.cc
    LIBRARIES_TO_LINK
        ${libcb-ergr}
        ${libaqua-sim-ng}
        ${libapplications}
        ${libnetwork}
        ${libmobility}
        ${libenergy}
)
