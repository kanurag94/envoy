#include <fstream>

#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.validate.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/base64.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/message_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/http/dynamic_modules/factory.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ReturnRef;

namespace Envoy {
namespace Server {
namespace Configuration {

class DynamicModuleFilterConfigTest : public Event::TestUsingSimulatedTime, public testing::Test {
protected:
  DynamicModuleFilterConfigTest() : api_(Api::createApiForTest(stats_store_)) {
    ON_CALL(context_.server_factory_context_, api()).WillByDefault(ReturnRef(*api_));
    ON_CALL(context_, scope()).WillByDefault(ReturnRef(stats_scope_));
    ON_CALL(context_, listenerInfo()).WillByDefault(ReturnRef(listener_info_));
    ON_CALL(listener_info_, metadata()).WillByDefault(ReturnRef(listener_metadata_));
    EXPECT_CALL(context_, initManager()).WillRepeatedly(ReturnRef(init_manager_));
    ON_CALL(context_.server_factory_context_, clusterManager())
        .WillByDefault(ReturnRef(cluster_manager_));
    ON_CALL(context_.server_factory_context_, mainThreadDispatcher())
        .WillByDefault(ReturnRef(dispatcher_));
    // Clear the global module cache between tests to avoid cross-test interference.
    clearModuleCacheForTest();
  }

  NiceMock<Network::MockListenerInfo> listener_info_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::Scope& stats_scope_{*stats_store_.rootScope()};
  Api::ApiPtr api_;
  envoy::config::core::v3::Metadata listener_metadata_;
  Init::ManagerImpl init_manager_{"init_manager"};
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Init::ExpectableWatcherImpl init_watcher_;
  NiceMock<Event::MockDispatcher> dispatcher_;

  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(DynamicModuleFilterConfigTest, LegacyNameBasedLoading) {
  // Set up the search path to find the test module.
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: "no_op"
    do_not_close: true
  filter_name: "test_filter"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);

  TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
}

TEST_F(DynamicModuleFilterConfigTest, LocalFileLoading) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");

  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      local:
        filename: ")EOF",
                                                                    module_path, R"EOF("
    do_not_close: true
  filter_name: "test_filter"
  )EOF"));

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);
}

TEST_F(DynamicModuleFilterConfigTest, InlineBytesLoading) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");

  std::ifstream file(module_path, std::ios::binary);
  ASSERT_TRUE(file.good()) << "Failed to open test module: " << module_path;
  std::string module_bytes((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  ASSERT_FALSE(module_bytes.empty());

  const std::string yaml = absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      local:
        inline_bytes: ")EOF",
                                        Base64::encode(module_bytes.data(), module_bytes.size()),
                                        R"EOF("
    do_not_close: true
  filter_name: "test_filter"
  )EOF");

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);
}

TEST_F(DynamicModuleFilterConfigTest, NoModuleOrName) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    do_not_close: true
  filter_name: "test_filter"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_THAT(cb_or_error.status().message(),
              testing::HasSubstr("Either 'name' or 'module' must be specified"));
}

TEST_F(DynamicModuleFilterConfigTest, InvalidLocalFile) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    module:
      local:
        filename: "/nonexistent/path/to/module.so"
    do_not_close: true
  filter_name: "test_filter"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_THAT(cb_or_error.status().message(), testing::HasSubstr("Failed to read module data"));
}

TEST_F(DynamicModuleFilterConfigTest, RemoteLoadingWarmingModeSuccess) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");

  std::ifstream file(module_path, std::ios::binary);
  ASSERT_TRUE(file.good()) << "Failed to open test module: " << module_path;
  std::string module_bytes((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  ASSERT_FALSE(module_bytes.empty());

  const std::string sha256 = Hex::encode(
      Common::Crypto::UtilitySingleton::get().getSha256Digest(Buffer::OwnedImpl(module_bytes)));

  const std::string yaml = absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: )EOF",
                                        sha256, R"EOF(
    do_not_close: true
  filter_name: "test_filter"
  )EOF");

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Http::MockAsyncClient> client;
  NiceMock<Http::MockAsyncClientRequest> request(&client);

  cluster_manager_.initializeThreadLocalClusters({"cluster_1"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
      .WillOnce(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
            response->body().add(module_bytes);
            callbacks.onSuccess(request, std::move(response));
            return &request;
          }));

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher_, ready());
  init_manager_.initialize(init_watcher_);
  EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initialized);
}

TEST_F(DynamicModuleFilterConfigTest, RemoteLoadingWarmingModeFetchFailure) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");

  std::ifstream file(module_path, std::ios::binary);
  ASSERT_TRUE(file.good()) << "Failed to open test module: " << module_path;
  std::string module_bytes((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  ASSERT_FALSE(module_bytes.empty());

  const std::string sha256 = Hex::encode(
      Common::Crypto::UtilitySingleton::get().getSha256Digest(Buffer::OwnedImpl(module_bytes)));

  // Set num_retries: 0 so RemoteAsyncDataProvider won't try to use the retry timer.
  const std::string yaml = absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        retry_policy:
          num_retries: 0
        sha256: )EOF",
                                        sha256, R"EOF(
    do_not_close: true
  filter_name: "test_filter"
  )EOF");

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Http::MockAsyncClient> client;
  NiceMock<Http::MockAsyncClientRequest> request(&client);

  cluster_manager_.initializeThreadLocalClusters({"cluster_1"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
      .WillOnce(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "503"}}}));
            callbacks.onSuccess(request, std::move(response));
            return &request;
          }));

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher_, ready());
  init_manager_.initialize(init_watcher_);
  EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initialized);

  // Fetch failed — default is fail-closed, which adds a decoder filter that sends 503.
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_)).Times(1);
  cb_or_error.value()(filter_callback);
}

TEST_F(DynamicModuleFilterConfigTest, RemoteWithEmptySHA256) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
    do_not_close: true
  filter_name: "test_filter"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_THAT(cb_or_error.status().message(), testing::HasSubstr("SHA256 hash is required"));
}

TEST_F(DynamicModuleFilterConfigTest, RemoteLoadingSHA256Mismatch) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");

  std::ifstream file(module_path, std::ios::binary);
  ASSERT_TRUE(file.good()) << "Failed to open test module: " << module_path;
  std::string module_bytes((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  ASSERT_FALSE(module_bytes.empty());

  // Use an incorrect SHA256 hash that won't match the actual module bytes.
  const std::string wrong_sha256 =
      "0000000000000000000000000000000000000000000000000000000000000000";

  // Set num_retries: 0 so RemoteAsyncDataProvider won't try to use the retry timer.
  const std::string yaml = absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        retry_policy:
          num_retries: 0
        sha256: )EOF",
                                        wrong_sha256, R"EOF(
    do_not_close: true
  filter_name: "test_filter"
  )EOF");

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Http::MockAsyncClient> client;
  NiceMock<Http::MockAsyncClientRequest> request(&client);

  cluster_manager_.initializeThreadLocalClusters({"cluster_1"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
      .WillOnce(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
            response->body().add(module_bytes);
            callbacks.onSuccess(request, std::move(response));
            return &request;
          }));

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher_, ready());
  init_manager_.initialize(init_watcher_);
  EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initialized);

  // RemoteDataFetcher rejects the SHA256 mismatch, so filter_config stays null (fail-closed by default).
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_)).Times(1);
  cb_or_error.value()(filter_callback);
}

TEST_F(DynamicModuleFilterConfigTest, EmptyLocalModuleData) {
  const std::string empty_file = TestEnvironment::temporaryPath("empty_module.so");
  { std::ofstream f(empty_file); }

  const std::string yaml = absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      local:
        filename: ")EOF",
                                        empty_file, R"EOF("
    do_not_close: true
  filter_name: "test_filter"
  )EOF");

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_THAT(cb_or_error.status().message(), testing::HasSubstr("Module data is empty"));
}

TEST_F(DynamicModuleFilterConfigTest, ServerContextFactory) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: "no_op"
    do_not_close: true
  filter_name: "test_filter"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  EXPECT_FALSE(
      factory.isTerminalFilterByProtoTyped(proto_config, context_.server_factory_context_));
  EXPECT_NO_THROW(factory.createFilterFactoryFromProtoWithServerContextTyped(
      proto_config, "stats", context_.server_factory_context_));

  TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
}

TEST_F(DynamicModuleFilterConfigTest, ServerContextRemoteNoInitManager) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: "abc123"
    do_not_close: true
  filter_name: "test_filter"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  EXPECT_THROW(factory.createFilterFactoryFromProtoWithServerContextTyped(
                   proto_config, "stats", context_.server_factory_context_),
               EnvoyException);
}

TEST_F(DynamicModuleFilterConfigTest, RouteSpecificConfigPerRouteConfigFail) {
  // Set up the search path to find the test module.
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

  // http_filter_per_route_config_new_fail exports the per-route config symbol but returns nullptr.
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: "http_filter_per_route_config_new_fail"
    do_not_close: true
  per_route_config_name: "test"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilterPerRoute proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  NiceMock<ProtobufMessage::MockValidationVisitor> visitor;
  auto config_or_error = factory.createRouteSpecificFilterConfig(
      proto_config, context_.server_factory_context_, visitor);
  EXPECT_FALSE(config_or_error.ok());
  EXPECT_THAT(config_or_error.status().message(),
              testing::HasSubstr("Failed to create pre-route filter config"));

  TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
}

TEST_F(DynamicModuleFilterConfigTest, RouteSpecificConfigInvalidModule) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: "nonexistent_module"
    do_not_close: true
  per_route_config_name: "test"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilterPerRoute proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  NiceMock<ProtobufMessage::MockValidationVisitor> visitor;
  auto config_or_error = factory.createRouteSpecificFilterConfig(
      proto_config, context_.server_factory_context_, visitor);
  EXPECT_FALSE(config_or_error.ok());
  EXPECT_THAT(config_or_error.status().message(),
              testing::HasSubstr("Failed to load dynamic module"));
}

TEST_F(DynamicModuleFilterConfigTest, RemoteNackOnCacheMiss) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");

  std::ifstream file(module_path, std::ios::binary);
  ASSERT_TRUE(file.good()) << "Failed to open test module: " << module_path;
  std::string module_bytes((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  ASSERT_FALSE(module_bytes.empty());

  const std::string sha256 = Hex::encode(
      Common::Crypto::UtilitySingleton::get().getSha256Digest(Buffer::OwnedImpl(module_bytes)));

  const std::string yaml = absl::StrCat(R"EOF(
  dynamic_module_config:
    nack_on_module_cache_miss: true
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: )EOF",
                                        sha256, R"EOF(
    do_not_close: true
  filter_name: "test_filter"
  )EOF");

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Http::MockAsyncClient> client;
  NiceMock<Http::MockAsyncClientRequest> request(&client);

  cluster_manager_.initializeThreadLocalClusters({"cluster_1"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
      .WillOnce(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));

  // Capture the callback so we can simulate background fetch completion later.
  Http::AsyncClient::Callbacks* fetch_callbacks = nullptr;
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            fetch_callbacks = &callbacks;
            return &request;
          }));

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);

  // First push should NACK (UnavailableError).
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_TRUE(absl::IsUnavailable(cb_or_error.status()));
  EXPECT_THAT(cb_or_error.status().message(), testing::HasSubstr("not in cache"));

  // Background fetch was started.
  ASSERT_NE(fetch_callbacks, nullptr);

  // Complete the background fetch — this populates the cache.
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  response->body().add(module_bytes);
  fetch_callbacks->onSuccess(request, std::move(response));
}

TEST_F(DynamicModuleFilterConfigTest, RemoteNackInProgressDedup) {
  // When a fetch is already in_progress, a second NACK request should NOT start a duplicate fetch.
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");

  std::ifstream file(module_path, std::ios::binary);
  ASSERT_TRUE(file.good()) << "Failed to open test module: " << module_path;
  std::string module_bytes((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  ASSERT_FALSE(module_bytes.empty());

  const std::string sha256 = Hex::encode(
      Common::Crypto::UtilitySingleton::get().getSha256Digest(Buffer::OwnedImpl(module_bytes)));

  const std::string yaml = absl::StrCat(R"EOF(
  dynamic_module_config:
    nack_on_module_cache_miss: true
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: )EOF",
                                        sha256, R"EOF(
    do_not_close: true
  filter_name: "test_filter"
  )EOF");

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Http::MockAsyncClient> client;
  NiceMock<Http::MockAsyncClientRequest> request(&client);

  cluster_manager_.initializeThreadLocalClusters({"cluster_1"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
      .WillOnce(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));

  // Only one fetch should be started — capture to verify.
  Http::AsyncClient::Callbacks* fetch_callbacks = nullptr;
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            fetch_callbacks = &callbacks;
            return &request;
          }));

  DynamicModuleConfigFactory factory;

  // First push — starts background fetch, returns NACK.
  auto cb_or_error1 = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error1.ok());
  EXPECT_TRUE(absl::IsUnavailable(cb_or_error1.status()));
  ASSERT_NE(fetch_callbacks, nullptr);

  // Second push while fetch is still in_progress — NACKs but does NOT start another fetch.
  // The mock has WillOnce, so a second send_() call would fail the test.
  auto cb_or_error2 = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error2.ok());
  EXPECT_TRUE(absl::IsUnavailable(cb_or_error2.status()));

  // Complete the fetch.
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  response->body().add(module_bytes);
  fetch_callbacks->onSuccess(request, std::move(response));
}

TEST_F(DynamicModuleFilterConfigTest, RemoteNackThenCacheHit) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");

  std::ifstream file(module_path, std::ios::binary);
  ASSERT_TRUE(file.good()) << "Failed to open test module: " << module_path;
  std::string module_bytes((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  ASSERT_FALSE(module_bytes.empty());

  const std::string sha256 = Hex::encode(
      Common::Crypto::UtilitySingleton::get().getSha256Digest(Buffer::OwnedImpl(module_bytes)));

  const std::string yaml = absl::StrCat(R"EOF(
  dynamic_module_config:
    nack_on_module_cache_miss: true
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: )EOF",
                                        sha256, R"EOF(
    do_not_close: true
  filter_name: "test_filter"
  )EOF");

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Http::MockAsyncClient> client;
  NiceMock<Http::MockAsyncClientRequest> request(&client);

  cluster_manager_.initializeThreadLocalClusters({"cluster_1"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
      .WillOnce(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));

  // Capture the callback so we can simulate background fetch completion.
  Http::AsyncClient::Callbacks* fetch_callbacks = nullptr;
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            fetch_callbacks = &callbacks;
            return &request;
          }));

  DynamicModuleConfigFactory factory;

  // First push — NACKs.
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_TRUE(absl::IsUnavailable(cb_or_error.status()));

  // Complete the background fetch to fill the cache.
  ASSERT_NE(fetch_callbacks, nullptr);
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  response->body().add(module_bytes);
  fetch_callbacks->onSuccess(request, std::move(response));

  // Second push (simulating control plane retry) — cache hit, should succeed.
  auto cb_or_error2 = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error2.ok()) << cb_or_error2.status().message();
}

TEST_F(DynamicModuleFilterConfigTest, RemoteNackFetchFailureThenRetry) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");

  std::ifstream file(module_path, std::ios::binary);
  ASSERT_TRUE(file.good()) << "Failed to open test module: " << module_path;
  std::string module_bytes((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  ASSERT_FALSE(module_bytes.empty());

  const std::string sha256 = Hex::encode(
      Common::Crypto::UtilitySingleton::get().getSha256Digest(Buffer::OwnedImpl(module_bytes)));

  const std::string yaml = absl::StrCat(R"EOF(
  dynamic_module_config:
    nack_on_module_cache_miss: true
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: )EOF",
                                        sha256, R"EOF(
    do_not_close: true
  filter_name: "test_filter"
  )EOF");

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Http::MockAsyncClient> client;
  NiceMock<Http::MockAsyncClientRequest> request(&client);

  cluster_manager_.initializeThreadLocalClusters({"cluster_1"});

  // First push — starts background fetch.
  Http::AsyncClient::Callbacks* fetch_callbacks1 = nullptr;
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
      .WillOnce(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            fetch_callbacks1 = &callbacks;
            return &request;
          }));

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_TRUE(absl::IsUnavailable(cb_or_error.status()));

  // Background fetch fails.
  ASSERT_NE(fetch_callbacks1, nullptr);
  fetch_callbacks1->onFailure(request, Http::AsyncClient::FailureReason::Reset);

  // Second push — should start a new background fetch (not stuck in in_progress).
  Http::AsyncClient::Callbacks* fetch_callbacks2 = nullptr;
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
      .WillOnce(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            fetch_callbacks2 = &callbacks;
            return &request;
          }));

  auto cb_or_error2 = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error2.ok());
  EXPECT_TRUE(absl::IsUnavailable(cb_or_error2.status()));

  // A new fetch was started.
  EXPECT_NE(fetch_callbacks2, nullptr);

  // Complete the second fetch successfully.
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  response->body().add(module_bytes);
  fetch_callbacks2->onSuccess(request, std::move(response));

  // Third push — cache hit.
  auto cb_or_error3 = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error3.ok()) << cb_or_error3.status().message();
}

TEST_F(DynamicModuleFilterConfigTest, RemoteNackCacheHitLoadFailure) {
  // If cached bytes are present but fail to load as a module (e.g. corrupted .so),
  // the error should be returned on the cache-hit path.
  const std::string garbage_bytes = "this is not a valid shared object";
  const std::string garbage_sha256 = Hex::encode(
      Common::Crypto::UtilitySingleton::get().getSha256Digest(Buffer::OwnedImpl(garbage_bytes)));

  const std::string yaml = absl::StrCat(R"EOF(
  dynamic_module_config:
    nack_on_module_cache_miss: true
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: )EOF",
                                        garbage_sha256, R"EOF(
    do_not_close: true
  filter_name: "test_filter"
  )EOF");

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Http::MockAsyncClient> client;
  NiceMock<Http::MockAsyncClientRequest> request(&client);

  cluster_manager_.initializeThreadLocalClusters({"cluster_1"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
      .WillOnce(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));

  // First push — NACK, starts background fetch.
  Http::AsyncClient::Callbacks* fetch_callbacks = nullptr;
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            fetch_callbacks = &callbacks;
            return &request;
          }));

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_TRUE(absl::IsUnavailable(cb_or_error.status()));

  // Background fetch "succeeds" with SHA256-matching but invalid .so bytes.
  ASSERT_NE(fetch_callbacks, nullptr);
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  response->body().add(garbage_bytes);
  fetch_callbacks->onSuccess(request, std::move(response));

  // Second push — cache hit with garbage bytes. createFilterConfigFromBytes should fail.
  auto cb_or_error2 = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error2.ok());
  EXPECT_THAT(cb_or_error2.status().message(), testing::HasSubstr("Failed to load dynamic module"));
}

TEST_F(DynamicModuleFilterConfigTest, RemoteWarmingFailClosed) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");

  std::ifstream file(module_path, std::ios::binary);
  ASSERT_TRUE(file.good()) << "Failed to open test module: " << module_path;
  std::string module_bytes((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  ASSERT_FALSE(module_bytes.empty());

  const std::string sha256 = Hex::encode(
      Common::Crypto::UtilitySingleton::get().getSha256Digest(Buffer::OwnedImpl(module_bytes)));

  // Set num_retries: 0 so RemoteAsyncDataProvider won't try to use the retry timer.
  const std::string yaml = absl::StrCat(R"EOF(
  dynamic_module_config:
    failure_policy: FAIL_CLOSED
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        retry_policy:
          num_retries: 0
        sha256: )EOF",
                                        sha256, R"EOF(
    do_not_close: true
  filter_name: "test_filter"
  )EOF");

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Http::MockAsyncClient> client;
  NiceMock<Http::MockAsyncClientRequest> request(&client);

  cluster_manager_.initializeThreadLocalClusters({"cluster_1"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
      .WillOnce(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            // Return 503 to simulate fetch failure.
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "503"}}}));
            callbacks.onSuccess(request, std::move(response));
            return &request;
          }));

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher_, ready());
  init_manager_.initialize(init_watcher_);
  EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initialized);

  // Fetch failed — fail-closed should add a decoder filter that sends 503.
  Http::StreamDecoderFilterSharedPtr captured_filter;
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_))
      .WillOnce(testing::Invoke(
          [&](Http::StreamDecoderFilterSharedPtr filter) { captured_filter = filter; }));
  cb_or_error.value()(filter_callback);

  // Verify the filter actually sends 503.
  ASSERT_NE(captured_filter, nullptr);
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  captured_filter->setDecoderFilterCallbacks(decoder_callbacks);
  EXPECT_CALL(decoder_callbacks, sendLocalReply(Http::Code::ServiceUnavailable, _, _, _, _));
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            captured_filter->decodeHeaders(headers, true));
}

TEST_F(DynamicModuleFilterConfigTest, RemoteWarmingFailOpen) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");

  std::ifstream file(module_path, std::ios::binary);
  ASSERT_TRUE(file.good()) << "Failed to open test module: " << module_path;
  std::string module_bytes((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  ASSERT_FALSE(module_bytes.empty());

  const std::string sha256 = Hex::encode(
      Common::Crypto::UtilitySingleton::get().getSha256Digest(Buffer::OwnedImpl(module_bytes)));

  // Set num_retries: 0 so RemoteAsyncDataProvider won't try to use the retry timer.
  const std::string yaml = absl::StrCat(R"EOF(
  dynamic_module_config:
    failure_policy: FAIL_OPEN
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        retry_policy:
          num_retries: 0
        sha256: )EOF",
                                        sha256, R"EOF(
    do_not_close: true
  filter_name: "test_filter"
  )EOF");

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Http::MockAsyncClient> client;
  NiceMock<Http::MockAsyncClientRequest> request(&client);

  cluster_manager_.initializeThreadLocalClusters({"cluster_1"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
      .WillOnce(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "503"}}}));
            callbacks.onSuccess(request, std::move(response));
            return &request;
          }));

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher_, ready());
  init_manager_.initialize(init_watcher_);
  EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initialized);

  // Fetch failed with fail-open — callback is a no-op (no filter added).
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_)).Times(0);
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_)).Times(0);
  cb_or_error.value()(filter_callback);
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
