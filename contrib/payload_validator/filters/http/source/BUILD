load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

#envoy_cc_library(
envoy_cc_contrib_extension(
    name = "payload_validator_filter_lib",
    srcs = [
        "config.cc",
        "filter.cc",
        "validator.cc",
    ],
    hdrs = [
        "config.h",
        "filter.h",
        "validator.h",
    ],
    external_deps = [
        "json_schema_validator",
    ],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/runtime:runtime_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:exception_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/payload_validator/v3:pkg_cc_proto",
    ],
)
