#ifndef KCUCKOOUNTER_TESTS_IMAGE_SOURCE_TESTS_HPP
#define KCUCKOOUNTER_TESTS_IMAGE_SOURCE_TESTS_HPP

#include <QObject>

class image_source_tests : public QObject {
    Q_OBJECT
private slots:
    void source_acquisition_is_asynchronous_and_shares_content();
    void source_queue_replaces_obsolete_intent_data();
    void source_queue_replaces_obsolete_intent();
    void source_started_observer_can_retire_data();
    void source_started_observer_can_retire();
    void source_finished_observer_can_retarget_data();
    void source_finished_observer_can_retarget();
    void source_acquisition_errors_recover();
    void source_acquisition_rejects_foreign_thread_access();
    void source_replacement_has_new_content_identity();
    void source_identity_does_not_need_path_hash_labels();
    void decodes_svgz_and_rejects_truncation();
    void invalid_svg_reports_failure();
    void appearance_svg_views_share_application_service();
    void appearance_svg_view_source_and_lifetime();
};

#endif
