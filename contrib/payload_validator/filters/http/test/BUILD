load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

#envoy_cc_test(
#    name = "postgres_decoder_tests",
#    srcs = [
#        "postgres_decoder_test.cc",
#    ],
#    deps = [
#        ":postgres_test_utils_lib",
#        "//contrib/postgres_proxy/filters/network/source:filter",
#        "//test/mocks/network:network_mocks",
#    ],
#)

#envoy_proto_library(
#    name = "postgres_integration_proto",
#    srcs = [":postgres_integration_test.proto"],
#)

envoy_cc_test(
    name = "config_tests",
    srcs = [
        "config_test.cc",
    ],
    deps = [
        "//contrib/payload_validator/filters/http/source:payload_validator_filter_lib",
    ],
)

envoy_cc_test(
    name = "filter_tests",
    srcs = [
        "filter_test.cc",
    ],
    deps = [
        "//contrib/payload_validator/filters/http/source:payload_validator_filter_lib",
        "//test/mocks/http:http_mocks",
    ],
)
