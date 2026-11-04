# CB-ERGR v5 module (contrib/cb-ergr). Depends on aqua-sim-ng.
# Files are added step by step (kickoff section 4, steps D0..D7).
build_lib(
    LIBNAME cb-ergr
    SOURCE_FILES
        model/cb-ergr-config.cc
        model/acoustic-link-model.cc
        model/gm-shadowing.cc
        model/cb-ergr-propagation.cc
        helper/cb-ergr-helper.cc
    HEADER_FILES
        model/cb-ergr-config.h
        model/cb-ergr-params.h
        model/small-matrix.h
        model/serial-number.h
        model/receipt-window.h
        model/energy-price.h
        model/cb-learner-model.h
        model/acoustic-link-model.h
        model/gm-shadowing.h
        model/cb-ergr-propagation.h
        helper/cb-ergr-helper.h
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

build_lib_example(
    NAME link-per-curve
    SOURCE_FILES examples/link-per-curve.cc
    LIBRARIES_TO_LINK
        ${libcb-ergr}
        ${libaqua-sim-ng}
        ${libnetwork}
        ${libmobility}
        ${libenergy}
)
