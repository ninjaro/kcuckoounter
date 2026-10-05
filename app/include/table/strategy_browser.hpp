#ifndef KCUCKOOUNTER_TABLE_STRATEGY_BROWSER_HPP
#define KCUCKOOUNTER_TABLE_STRATEGY_BROWSER_HPP

#include "settings/strategy_data.hpp"

class QWidget;

/** Read-only reference browser; selection and settings remain caller-owned. */
QWidget* create_strategy_browser(
    const strategy_catalog& catalog, const QString& selected_slug,
    QWidget* parent = nullptr
);

#endif // KCUCKOOUNTER_TABLE_STRATEGY_BROWSER_HPP
