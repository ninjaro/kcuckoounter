#ifndef KCUCKOOUNTER_TESTS_STRATEGY_DATA_TESTS_HPP
#define KCUCKOOUNTER_TESTS_STRATEGY_DATA_TESTS_HPP

#include <QObject>

class strategy_data_tests : public QObject {
    Q_OBJECT

private slots:
    void bundled_catalog_is_valid_and_cached();
    void parser_accepts_optional_publication_date();
    void parser_rejects_malformed_and_semantically_invalid_documents();
    void parser_rejects_duplicate_ids_and_slugs();
    void initial_count_formulas_are_decoded_and_evaluated();
    void parser_rejects_unsupported_initial_count_formulas();
    void parser_requires_the_canonical_rank_order();
    void bundled_initial_counts_and_recommendations_are_audited();
};

#endif // KCUCKOOUNTER_TESTS_STRATEGY_DATA_TESTS_HPP
