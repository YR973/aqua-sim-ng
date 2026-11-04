# CB-ERGR v5 module (contrib/cb-ergr). Depends on aqua-sim-ng.
# Files are added step by step (kickoff section 4, steps D0..D7).
build_lib(
    LIBNAME cb-ergr
    SOURCE_FILES
        model/cb-ergr-config.cc
        model/acoustic-link-model.cc
        model/gm-shadowing.cc
        model/tx-power-level-tag.cc
        model/cb-ergr-propagation.cc
        model/grid-geometry.cc
        model/ergr-headers.cc
        model/neighbour-table.cc
        model/forwarder-selector.cc
        model/selector-round-robin.cc
        model/cb-features.cc
        model/selector-fixed-weight.cc
        model/selector-carp-ewma.cc
        model/selector-q-learning.cc
        model/selector-lfa-q.cc
        model/oracle-evaluator.cc
        model/selector-oracle.cc
        model/selector-cb-v5.cc
        model/localisation-noise.cc
        model/current-drift-mobility.cc
        model/metrics-collector.cc
        model/feedback-manager.cc
        model/ergr-emhc-routing.cc
        model/scenario-config.cc
        helper/cb-ergr-helper.cc
        helper/scenario-runner.cc
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
        model/tx-power-level-tag.h
        model/cb-ergr-propagation.h
        model/grid-geometry.h
        model/ergr-headers.h
        model/neighbour-table.h
        model/forwarder-selector.h
        model/selector-round-robin.h
        model/cb-features.h
        model/selector-fixed-weight.h
        model/selector-carp-ewma.h
        model/selector-q-learning.h
        model/selector-lfa-q.h
        model/oracle-evaluator.h
        model/selector-oracle.h
        model/selector-cb-v5.h
        model/localisation-noise.h
        model/current-drift-mobility.h
        model/metrics-collector.h
        model/feedback-manager.h
        model/held-packet.h
        model/recent-set.h
        model/ergr-emhc-routing.h
        model/scenario-config.h
        helper/cb-ergr-helper.h
        helper/scenario-runner.h
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

build_lib_example(
    NAME cb-ergr-scenario
    SOURCE_FILES examples/cb-ergr-scenario.cc
    LIBRARIES_TO_LINK
        ${libcb-ergr}
        ${libaqua-sim-ng}
        ${libnetwork}
        ${libmobility}
        ${libenergy}
)
