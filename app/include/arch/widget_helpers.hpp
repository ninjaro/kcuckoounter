#ifndef KCUCKOOUNTER_ARCH_WIDGET_HELPERS_HPP
#define KCUCKOOUNTER_ARCH_WIDGET_HELPERS_HPP

#include "arch/platform.hpp"

#ifdef KC_KDE
#include <KToolBar>
#include <KXmlGuiWindow>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
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
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#ifdef KC_KDE
using BaseMainWindow = KXmlGuiWindow; // Alternatives: QMainWindow
using BaseCheckBox = QCheckBox; // Alternatives: KToggleAction
using BaseComboBox = QComboBox;
using BasePushButton = QPushButton; // Alternatives: QPushButton
using BaseToolBar = KToolBar; // Alternatives: QToolBar
#else
using BaseMainWindow = QMainWindow;
using BaseCheckBox = QCheckBox;
using BaseComboBox = QComboBox;
using BasePushButton = QPushButton;
using BaseToolBar = QToolBar;
#endif

using BaseAction = QAction;
using BaseFormLayout = QFormLayout;
using BaseSpinBox = QSpinBox;
using BaseVBoxLayout = QVBoxLayout;
using BaseWidget = QWidget;

#endif // KCUCKOOUNTER_ARCH_WIDGET_HELPERS_HPP
