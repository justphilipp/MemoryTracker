// Copyright (c) 2020-present,  INSPUR Co, Ltd.  All rights reserved.
#ifndef COMMON_SRC_H_CM_METRICS_PLUGIN_H_
#define COMMON_SRC_H_CM_METRICS_PLUGIN_H_

#include <memory>
#include <vector>
#include <string>

#include "cm_metrics.h"
#include "cm_kwdb_context.h"

#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/aggregation/default_aggregation.h"
#include "opentelemetry/sdk/metrics/aggregation/histogram_aggregation.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader.h"
#include "opentelemetry/sdk/metrics/meter.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h"

#define TIMEINTERVAL 5000
#define TIMEOUT 3000

namespace kwdbts {
namespace otlp = opentelemetry::exporter::otlp;
namespace metric_sdk = opentelemetry::sdk::metrics;
namespace common = opentelemetry::common;
namespace metrics_api = opentelemetry::metrics;
namespace nostd = opentelemetry::nostd;
//! Allocate space for MetricsPlugin instance.
KStatus InitMetricsPlugin(kwdbContext_p ctx);
//! Free MetricsPlugin instance.
KStatus DestroyMetricsPlugin(kwdbContext_p ctx);

//! Abstract base class for metrics module.
class MetricsPlugin {
 public:
  ~MetricsPlugin() = default;
  // Create opentelemetry-gauge interface.
  void CreateOtlpGauge(const Gauge &);
  // Create opentelemetry-counter interface.
  void CreateOtlpCounter(const Counter &);
  // Shutdown opentelemetry-exporter
  KStatus ShutdownExporter();
  // Restart opentelemetry-exporter
  KStatus RestartExporter();
  // update opentelemetry-exporter report interval
  KStatus UpdateReportInterval(k_uint32 seconds);

  static std::atomic_bool running_;
 private:
  std::vector<nostd::shared_ptr<metrics_api::ObservableInstrument>> metricObjs_;
};

extern KStatus RegisterGaugeForExport(std::shared_ptr<Gauge> metric);
extern KStatus RegisterCounterForExport(std::shared_ptr<Counter> metric);
extern KStatus ShutdownMetricsService();
extern KStatus RestartMetricsService();
extern void BatchRegisterMetrics(KString clusterId);

extern KStatus UpdateMetricsServiceInterval(k_uint32 seconds);
}  // namespace kwdbts
#endif  // COMMON_SRC_H_CM_METRICS_PLUGIN_H_
