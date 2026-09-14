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
};

#endif // KCUCKOOUNTER_TESTS_STRATEGY_DATA_TESTS_HPP
