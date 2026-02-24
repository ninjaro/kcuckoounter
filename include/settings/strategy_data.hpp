#ifndef KCUCKOOUNTER_INCLUDE_SETTINGS_STRATEGY_DATA_HPP
#define KCUCKOOUNTER_INCLUDE_SETTINGS_STRATEGY_DATA_HPP

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

QVector<strategy_data> load_strategies();
QMap<QString, QString> load_strategy_key_descriptions();

#endif // KCUCKOOUNTER_INCLUDE_SETTINGS_STRATEGY_DATA_HPP
