#ifndef KCUCKOOUNTER_HELPERS_WIDGET_HELPERS_HPP
#define KCUCKOOUNTER_HELPERS_WIDGET_HELPERS_HPP

#ifdef KC_KDE
#include <KToolBar>
#include <KXmlGuiWindow>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#elif defined(Q_OS_ANDROID)
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QMainWindow>
#include <QToolBar>
#include <QToolButton>
#else // Default Qt
#include <QCheckBox>
#include <QComboBox>
#include <QMainWindow>
#include <QPushButton>
#include <QToolBar>
#endif

#include <QAction>
#include <QFormLayout>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include "helpers/base_clock.hpp"
#include "helpers/time_interface.hpp"

#ifdef KC_KDE
using BaseMainWindow = KXmlGuiWindow; // Alternatives: QMainWindow
using BaseCheckBox = QCheckBox; // Alternatives: KToggleAction
using BaseComboBox = QComboBox;
using BasePushButton = QPushButton; // Alternatives: QPushButton
using BaseToolBar = KToolBar; // Alternatives: QToolBar
#elif defined(Q_OS_ANDROID)
using BaseMainWindow = QMainWindow;
using BaseCheckBox = QCheckBox;
using BaseComboBox = QComboBox;
using BasePushButton = QToolButton; // Alternatives: QPushButton
using BaseToolBar = QToolBar;
#else
using BaseMainWindow = QMainWindow;
using BaseCheckBox = QCheckBox;
using BaseComboBox = QComboBox;
using BasePushButton = QPushButton;
using BaseToolBar = QToolBar;
#endif

using BaseAction = QAction;
using BaseClock = ::BaseClock;
using BaseFormLayout = QFormLayout;
using BaseSpinBox = QSpinBox;
using BaseVBoxLayout = QVBoxLayout;
using BaseWidget = QWidget;

#endif // KCUCKOOUNTER_HELPERS_WIDGET_HELPERS_HPP
