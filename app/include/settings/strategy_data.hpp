#ifndef KCUCKOOUNTER_SETTINGS_STRATEGY_DATA_HPP
#define KCUCKOOUNTER_SETTINGS_STRATEGY_DATA_HPP

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

struct strategy_data {
    int id = 0;
    QString slug;
    QString name;
    QString date;
    QString description;
    QStringList authors;
    QStringList games;
    int min_decks = 0;
    bool balance = false;
    bool ace_neutral = false;
    QVector<int> weights;
    QMap<QString, double> metrics;
    QMap<QString, QString> unique_fields;

    struct strategy_reference {
        QString type;
        QString citation;
        QString url;
        QString accessed;
    };

    QVector<strategy_reference> references;
};

struct strategy_catalog {
    QVector<strategy_data> strategies;
    QMap<QString, QString> key_descriptions;
    QStringList diagnostics;

    [[nodiscard]] bool is_valid() const;
    [[nodiscard]] QString diagnostic_summary() const;
};

strategy_catalog parse_strategy_catalog(const QByteArray& json_data);
const strategy_catalog& strategy_repository();

#endif // KCUCKOOUNTER_SETTINGS_STRATEGY_DATA_HPP
