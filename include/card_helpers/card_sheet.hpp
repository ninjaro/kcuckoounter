#ifndef KCUCKOOUNTER_CARD_HELPERS_CARD_SHEET_HPP
#define KCUCKOOUNTER_CARD_HELPERS_CARD_SHEET_HPP

#include <QString>
#include <QStringList>
#include <utility>

QString card_sheet_source_path();
bool preload_card_sheet();
std::pair<int, int> card_sheet_ratio();
QString card_label_from_index(int index);
QString card_element_id_from_index(int index);
const QStringList& card_element_ids();
QString card_back_element_id();

#endif // KCUCKOOUNTER_CARD_HELPERS_CARD_SHEET_HPP
