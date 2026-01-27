load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

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

envoy_cc_test(
    name = "validator_tests",
    srcs = [
        "validator_test.cc",
    ],
    deps = [
        "//contrib/payload_validator/filters/http/source:payload_validator_filter_lib",
        "//test/mocks/http:http_mocks",
    ],
)

envoy_cc_test(
    name = "integration_tests",
    srcs = [
        "integration_test.cc",
    ],
    deps = [
        "//contrib/payload_validator/filters/http/source:payload_validator_filter_lib",
        "//test/integration:http_integration_lib",
    ],
)
