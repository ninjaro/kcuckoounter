#ifndef KCUCKOOUNTER_TESTS_DEBUG_PROBE_CORE_TESTS_HPP
#define KCUCKOOUNTER_TESTS_DEBUG_PROBE_CORE_TESTS_HPP

#include <QObject>

class debug_probe_core_tests : public QObject {
    Q_OBJECT

private slots:
    void cadence_mode_labels_are_stable();
    void process_sample_interval_matches_cadence_mode();
    void auto_process_report_policy_matches_contract();
    void protocol_v1_contract_freezes_core_fields();
    void protocol_v1_capabilities_expose_metric_catalog();
    void protocol_metric_hint_lookup_returns_catalog_entry();
    void metric_point_helpers_extract_and_merge_primary_metrics();
    void protocol_v1_message_builder_wraps_payload_and_identity();
    void telemetry_semantics_maps_expose_required_labels();
    void snapshot_export_json_contains_expected_sections();
    void process_memory_report_json_contains_expected_sections();
};

#endif // KCUCKOOUNTER_TESTS_DEBUG_PROBE_CORE_TESTS_HPP
