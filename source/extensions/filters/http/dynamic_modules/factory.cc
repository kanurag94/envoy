#include "source/extensions/filters/http/dynamic_modules/factory.h"

#include <functional>
#include <mutex>

#include "source/common/common/macros.h"
#include "source/common/config/datasource.h"
#include "source/common/config/remote_data_fetcher.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/filters/http/dynamic_modules/filter.h"
#include "source/extensions/filters/http/dynamic_modules/filter_config.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Server {
namespace Configuration {

namespace {

// Minimal in-memory cache for remotely fetched module bytes, keyed by SHA256.
// SHA256-addressed content is immutable, so no TTL is needed.
// Failed fetches leave `code` empty; the next config push retries.
struct ModuleCacheEntry {
  std::string code;
  bool in_progress{false};
};

std::mutex& moduleCacheMutex() { MUTABLE_CONSTRUCT_ON_FIRST_USE(std::mutex); }

absl::flat_hash_map<std::string, ModuleCacheEntry>& moduleCache() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(absl::flat_hash_map<std::string, ModuleCacheEntry>);
}

// A simple decoder filter that rejects all requests with 503 Service Unavailable.
// Used in fail-closed mode when a remote module fails to load.
class FailClosedDecoderFilter : public Http::PassThroughDecoderFilter {
public:
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable,
                                       "Dynamic module not available", nullptr, absl::nullopt,
                                       "dynamic_module_not_loaded");
    return Http::FilterHeadersStatus::StopIteration;
  }
};

// Bridges RemoteDataFetcherCallback to a lambda for background fetches.
// Extends DeferredDeletable for safe async cleanup via dispatcher.deferredDelete().
class RemoteDataFetcherAdapter : public Config::DataFetcher::RemoteDataFetcherCallback,
                                 public Event::DeferredDeletable {
public:
  RemoteDataFetcherAdapter(std::function<void(const std::string&)> cb) : cb_(std::move(cb)) {}
  ~RemoteDataFetcherAdapter() override = default;
  void onSuccess(const std::string& data) override { cb_(data); }
  void onFailure(Config::DataFetcher::FailureReason) override { cb_(""); }
  void setFetcher(std::unique_ptr<Config::DataFetcher::RemoteDataFetcher>&& fetcher) {
    fetcher_ = std::move(fetcher);
  }

private:
  std::function<void(const std::string&)> cb_;
  std::unique_ptr<Config::DataFetcher::RemoteDataFetcher> fetcher_;
};

absl::StatusOr<
    Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilterConfigSharedPtr>
createFilterConfigFromBytes(absl::string_view module_bytes, absl::string_view sha256_hash,
                            const FilterConfig& proto_config,
                            Server::Configuration::ServerFactoryContext& context,
                            Stats::Scope& scope) {
  const auto& module_config = proto_config.dynamic_module_config();

  auto dynamic_module = Extensions::DynamicModules::newDynamicModuleFromBytes(
      module_bytes, sha256_hash, module_config.do_not_close(), module_config.load_globally());
  if (!dynamic_module.ok()) {
    return absl::InvalidArgumentError("Failed to load dynamic module from bytes: " +
                                      std::string(dynamic_module.status().message()));
  }

  std::string config;
  if (proto_config.has_filter_config()) {
    auto config_or_error = MessageUtil::anyToBytes(proto_config.filter_config());
    if (!config_or_error.ok()) {
      return config_or_error.status();
    }
    config = std::move(config_or_error.value());
  }

  const std::string metrics_namespace =
      module_config.metrics_namespace().empty()
          ? std::string(Extensions::DynamicModules::HttpFilters::DefaultMetricsNamespace)
          : module_config.metrics_namespace();

  return Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
      proto_config.filter_name(), config, metrics_namespace, proto_config.terminal_filter(),
      std::move(dynamic_module.value()), scope, context);
}

Http::FilterFactoryCb createFilterFactoryCallback(
    Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilterConfigSharedPtr
        filter_config) {
  return [config = std::move(filter_config)](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    const std::string& worker_name = callbacks.dispatcher().name();
    auto pos = worker_name.find_first_of('_');
    ENVOY_BUG(pos != std::string::npos, "worker name is not in expected format worker_{index}");
    uint32_t worker_index;
    if (!absl::SimpleAtoi(worker_name.substr(pos + 1), &worker_index)) {
      IS_ENVOY_BUG("failed to parse worker index from name");
    }
    auto filter =
        std::make_shared<Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilter>(
            config, config->stats_scope_->symbolTable(), worker_index);
    callbacks.addStreamFilter(filter);

    // The addStreamFilter() will call the setDecoderFilterCallbacks first then
    // setEncoderFilterCallbacks.
    // We can initialize the in-module filter after we have both callbacks to ensure the in module
    // filter can access all the necessary information during creation.
    filter->initializeInModuleFilter();
  };
}

} // namespace

void clearModuleCacheForTest() {
  std::lock_guard<std::mutex> lock(moduleCacheMutex());
  moduleCache().clear();
}

absl::StatusOr<Http::FilterFactoryCb> DynamicModuleConfigFactory::createFilterFactory(
    const FilterConfig& proto_config, const std::string&,
    Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope,
    Init::Manager* init_manager) {

  const auto& module_config = proto_config.dynamic_module_config();

  if (module_config.has_module()) {
    return createFilterFactoryFromAsyncDataSource(proto_config, context, scope, init_manager);
  }

  // Legacy path: load module by name.
  if (module_config.name().empty()) {
    return absl::InvalidArgumentError(
        "Either 'name' or 'module' must be specified in dynamic_module_config");
  }

  auto dynamic_module = Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());
  if (!dynamic_module.ok()) {
    return absl::InvalidArgumentError("Failed to load dynamic module: " +
                                      std::string(dynamic_module.status().message()));
  }

  std::string config;
  if (proto_config.has_filter_config()) {
    auto config_or_error = MessageUtil::anyToBytes(proto_config.filter_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    config = std::move(config_or_error.value());
  }

  // Use configured metrics namespace or fall back to the default.
  const std::string metrics_namespace =
      module_config.metrics_namespace().empty()
          ? std::string(Extensions::DynamicModules::HttpFilters::DefaultMetricsNamespace)
          : module_config.metrics_namespace();

  absl::StatusOr<
      Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilterConfigSharedPtr>
      filter_config =
          Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
              proto_config.filter_name(), config, metrics_namespace, proto_config.terminal_filter(),
              std::move(dynamic_module.value()), scope, context);

  if (!filter_config.ok()) {
    return absl::InvalidArgumentError("Failed to create filter config: " +
                                      std::string(filter_config.status().message()));
  }

  // When the runtime guard is enabled, register the metrics namespace as a custom stat namespace.
  // This causes the namespace prefix to be stripped from prometheus output and no envoy_ prefix
  // is added. This is the legacy behavior for backward compatibility.
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
    context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
  }

  return createFilterFactoryCallback(filter_config.value());
}

// Handles the AsyncDataSource-based module loading path (local files and remote HTTP).
// For remote sources, the server blocks during initialization (warming mode) until the
// fetch completes (or fails).
absl::StatusOr<Http::FilterFactoryCb>
DynamicModuleConfigFactory::createFilterFactoryFromAsyncDataSource(
    const FilterConfig& proto_config, Server::Configuration::ServerFactoryContext& context,
    Stats::Scope& scope, Init::Manager* init_manager) {

  const auto& module_config = proto_config.dynamic_module_config();
  const auto& async_source = module_config.module();

  const std::string metrics_namespace =
      module_config.metrics_namespace().empty()
          ? std::string(Extensions::DynamicModules::HttpFilters::DefaultMetricsNamespace)
          : module_config.metrics_namespace();

  if (async_source.has_local()) {
    // Only local.filename is supported. Inline bytes/strings are not a good practice
    // for binary module data.
    if (!async_source.local().has_filename()) {
      return absl::InvalidArgumentError(
          "Only local.filename is supported for module sources; "
          "inline_bytes and inline_string are not supported");
    }

    auto data_or_error = Config::DataSource::read(async_source.local(), true, context.api());
    if (!data_or_error.ok()) {
      return absl::InvalidArgumentError("Failed to read module data: " +
                                        std::string(data_or_error.status().message()));
    }

    const std::string& module_bytes = data_or_error.value();
    if (module_bytes.empty()) {
      return absl::InvalidArgumentError("Module data is empty");
    }

    auto filter_config =
        createFilterConfigFromBytes(module_bytes, "", proto_config, context, scope);
    if (!filter_config.ok()) {
      return filter_config.status();
    }

    if (Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
      context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
    }
    return createFilterFactoryCallback(filter_config.value());
  }

  if (async_source.has_remote()) {
    const auto& remote_source = async_source.remote();
    const std::string& sha256_hash = remote_source.sha256();

    if (sha256_hash.empty()) {
      return absl::InvalidArgumentError("SHA256 hash is required for remote module sources");
    }

    // NACK-on-cache-miss path: check cache, NACK if miss, background fetch to fill cache.
    if (module_config.nack_on_module_cache_miss()) {
      // Check the cache under the lock, but copy bytes out before releasing so we don't
      // hold the mutex during the expensive createFilterConfigFromBytes (which calls dlopen).
      std::string cached_code;
      bool need_fetch = false;
      {
        std::lock_guard<std::mutex> lock(moduleCacheMutex());
        auto& cache = moduleCache();
        auto it = cache.find(sha256_hash);

        if (it != cache.end() && !it->second.code.empty()) {
          // Cache hit — copy bytes out so we can release the lock before loading.
          cached_code = it->second.code;
        } else {
          // Cache miss — start background fetch if not already in progress.
          auto& entry = cache[sha256_hash];
          if (!entry.in_progress) {
            entry.in_progress = true;
            need_fetch = true;
          }
        }
      } // mutex released

      if (!cached_code.empty()) {
        // Cache hit — load synchronously (no mutex held).
        auto filter_config = createFilterConfigFromBytes(cached_code, sha256_hash,
                                                         proto_config, context, scope);
        if (!filter_config.ok()) {
          return filter_config.status();
        }
        if (Runtime::runtimeFeatureEnabled(
                "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
          context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
        }
        return createFilterFactoryCallback(filter_config.value());
      }

      if (need_fetch) {
        // Holder pattern (same as Wasm): shared_ptr<unique_ptr<DeferredDeletable>> keeps
        // the adapter alive during the async fetch; deferredDelete cleans up safely.
        auto holder = std::make_shared<std::unique_ptr<Event::DeferredDeletable>>();
        auto& dispatcher = context.mainThreadDispatcher();

        auto adapter = std::make_unique<RemoteDataFetcherAdapter>(
            [sha256 = sha256_hash, holder, &dispatcher](const std::string& data) {
              {
                std::lock_guard<std::mutex> lock(moduleCacheMutex());
                auto& e = moduleCache()[sha256];
                e.in_progress = false;
                e.code = data;
              }
              if (*holder) {
                dispatcher.deferredDelete(
                    Event::DeferredDeletablePtr{holder->release()});
              }
            });

        auto fetcher = std::make_unique<Config::DataFetcher::RemoteDataFetcher>(
            context.clusterManager(), remote_source.http_uri(), sha256_hash, *adapter);
        auto* fetcher_ptr = fetcher.get();
        adapter->setFetcher(std::move(fetcher));
        *holder = std::move(adapter);
        fetcher_ptr->fetch();
      }

      ENVOY_LOG_MISC(info, "Dynamic module cache miss for SHA256 {}, NACKing config", sha256_hash);
      return absl::UnavailableError(
          "Dynamic module not in cache, background fetch started. "
          "Config will be retried by control plane.");
    }

    // Warming mode: block server init until the fetch completes. The init manager will
    // not transition to Initialized until the RemoteAsyncDataProvider signals ready().
    if (init_manager == nullptr) {
      return absl::InvalidArgumentError(
          "Init manager required for warming mode with remote module sources");
    }

    const auto failure_policy = module_config.failure_policy();

    // AsyncLoadState is shared between the fetch callback (which populates filter_config)
    // and the returned factory callback (which reads it). Also prevents the
    // RemoteAsyncDataProvider from being destroyed before the fetch completes.
    struct AsyncLoadState {
      Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilterConfigSharedPtr filter_config;
      RemoteAsyncDataProviderPtr remote_provider;
    };
    auto state = std::make_shared<AsyncLoadState>();

    // SHA256 verification is handled by the underlying RemoteDataFetcher.
    // Capture a weak_ptr to break the reference cycle: state owns remote_provider,
    // and remote_provider's callback would otherwise prevent state from being freed.
    std::weak_ptr<AsyncLoadState> weak_state = state;
    state->remote_provider = std::make_unique<RemoteAsyncDataProvider>(
        context.clusterManager(), *init_manager, remote_source, context.mainThreadDispatcher(),
        context.api().randomGenerator(), false,
        [weak_state, sha256_hash, proto_config_copy = proto_config, &context, &scope,
         metrics_namespace](const std::string& data) {
          auto state = weak_state.lock();
          if (data.empty()) {
            ENVOY_LOG_MISC(warn, "Remote dynamic module fetch failed for SHA256 {}", sha256_hash);
            return;
          }
          if (!state) {
            return;
          }
          auto filter_config =
              createFilterConfigFromBytes(data, sha256_hash, proto_config_copy, context, scope);
          if (!filter_config.ok()) {
            ENVOY_LOG_MISC(warn,
                           "Remote dynamic module fetched but failed to load for SHA256 {}: {}",
                           sha256_hash, filter_config.status().message());
            return;
          }
          state->filter_config = filter_config.value();
          if (Runtime::runtimeFeatureEnabled(
                  "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
            context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
          }
        });

    // Factory callback that handles both fail-open and fail-closed policies.
    return [state, failure_policy](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      if (!state->filter_config) {
        using FailurePolicy =
            envoy::extensions::dynamic_modules::v3::FailurePolicy;
        if (failure_policy == FailurePolicy::FAIL_OPEN) {
          ENVOY_LOG_MISC(warn,
                         "Dynamic module filter skipped: remote module was not loaded (fail-open)");
          return;
        }
        // Default (UNSPECIFIED/FAIL_CLOSED): send 503 for all requests.
        ENVOY_LOG_MISC(warn,
                       "Dynamic module not loaded, sending 503 (fail-closed)");
        callbacks.addStreamDecoderFilter(std::make_shared<FailClosedDecoderFilter>());
        return;
      }
      createFilterFactoryCallback(state->filter_config)(callbacks);
    };
  }

  return absl::InvalidArgumentError("Invalid AsyncDataSource: neither local nor remote specified");
}

Envoy::Http::FilterFactoryCb
DynamicModuleConfigFactory::createFilterFactoryFromProtoWithServerContextTyped(
    const FilterConfig& proto_config, const std::string& stat_prefix,
    Server::Configuration::ServerFactoryContext& context) {
  auto cb_or_error = createFilterFactory(proto_config, stat_prefix, context, context.scope());
  THROW_IF_NOT_OK_REF(cb_or_error.status());
  return cb_or_error.value();
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
DynamicModuleConfigFactory::createRouteSpecificFilterConfigTyped(
    const RouteConfigProto& proto_config, Server::Configuration::ServerFactoryContext&,
    ProtobufMessage::ValidationVisitor&) {

  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module = Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());
  if (!dynamic_module.ok()) {
    return absl::InvalidArgumentError("Failed to load dynamic module: " +
                                      std::string(dynamic_module.status().message()));
  }

  std::string config;
  if (proto_config.has_filter_config()) {
    auto config_or_error = MessageUtil::anyToBytes(proto_config.filter_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    config = std::move(config_or_error.value());
  }

  absl::StatusOr<Envoy::Extensions::DynamicModules::HttpFilters::
                     DynamicModuleHttpPerRouteFilterConfigConstSharedPtr>
      filter_config =
          Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpPerRouteConfig(
              proto_config.per_route_config_name(), config, std::move(dynamic_module.value()));

  if (!filter_config.ok()) {
    return absl::InvalidArgumentError("Failed to create pre-route filter config: " +
                                      std::string(filter_config.status().message()));
  }
  return filter_config.value();
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
