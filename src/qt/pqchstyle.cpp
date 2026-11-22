// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/pqchstyle.h>

#include <QApplication>
#include <QColor>
#include <QGraphicsDropShadowEffect>
#include <QPalette>
#include <QWidget>

namespace {
struct Colours {
    QString page, surface, border, text, muted, tint, tint_text;
};

Colours Current()
{
    if (PQCHStyle::IsDark()) {
        return {"#151827", "#1f2337", "#2e3450", "#f1f5f9", "#94a3b8", "rgba(139, 92, 246, 70)", "#ddd6fe"};
    }
    return {"#f4f5fb", "#ffffff", "#e6e8f0", "#0f172a", "#64748b", "rgba(139, 92, 246, 34)", "#5b21b6"};
}
} // namespace

namespace PQCHStyle {

bool IsDark()
{
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

QString ScrollBarSheet()
{
    const bool dark{IsDark()};
    const QString track{dark ? QStringLiteral("#2e1065") : QStringLiteral("#ede9fe")};
    return QStringLiteral(R"(
        QScrollBar:vertical { background: %1; width: 14px; margin: 0; border: none; border-radius: 7px; }
        QScrollBar:horizontal { background: %1; height: 14px; margin: 0; border: none; border-radius: 7px; }
        QScrollBar::handle:vertical { background: #7c3aed; min-height: 36px; margin: 2px; border-radius: 5px; }
        QScrollBar::handle:horizontal { background: #7c3aed; min-width: 36px; margin: 2px; border-radius: 5px; }
        QScrollBar::handle:hover { background: #8b5cf6; }
        QScrollBar::handle:pressed { background: #6d28d9; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }
        QScrollBar::add-page, QScrollBar::sub-page { background: none; }
    )").arg(track);
}

QString ToolbarSheet()
{
    const Colours c{Current()};
    return QStringLiteral(R"(
        QToolBar { background: %1; border: none; border-bottom: 1px solid %2; padding: 6px 10px; spacing: 4px; }
        QToolBar QToolButton { color: %3; font-weight: 600; border: none; border-radius: 10px; padding: 6px 12px; margin: 0 1px; }
        QToolBar QToolButton:hover { background: %4; }
        QToolBar QToolButton:checked { background: %4; color: %5; border-bottom: 3px solid #8b5cf6; }
        QToolBar QToolButton:disabled { color: %6; }
    )").arg(c.surface, c.border, c.text, c.tint, c.tint_text, c.muted);
}

void ApplyWalletStyle(QWidget* wallet_view)
{
    const Colours c{Current()};
    QPalette pal{wallet_view->palette()};
    pal.setColor(QPalette::Window, QColor(c.page));
    wallet_view->setPalette(pal);
    wallet_view->setAutoFillBackground(true);

    wallet_view->setStyleSheet(QStringLiteral(R"(
        /* Overview: the balance card in the brand gradient, recent transactions on a white card */
        OverviewPage #frame { border: none; border-radius: 16px;
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #1e1b4b, stop:0.55 #3b2a6b, stop:1 #7c3aed); }
        OverviewPage #frame QLabel { color: white; background: transparent; }
        OverviewPage #frame #label_5 { font-size: 16px; font-weight: 700; }
        OverviewPage #frame #labelBalanceText, OverviewPage #frame #labelPendingText,
        OverviewPage #frame #labelImmatureText, OverviewPage #frame #labelTotalText { color: #c4b5fd; }
        OverviewPage #frame #labelTotal { font-size: 22px; color: #67e8f9; }
        OverviewPage #frame #line { background: rgba(255, 255, 255, 60); border: none; max-height: 1px; }
        OverviewPage #frame { padding: 10px 12px; }
        OverviewPage #frame_2 { background: %2; border: 1px solid %3; border-radius: 16px; padding: 10px 12px; }
        OverviewPage #label_4 { font-size: 16px; font-weight: 700; color: %4; }
        #labelWalletStatus, #labelTransactionsStatus { background: transparent; border: none; padding: 0; }

        /* Send / Receive: sections as cards */
        SendCoinsDialog #frameCoinControl, SendCoinsDialog #frameFee,
        ReceiveCoinsDialog #frame2, ReceiveCoinsDialog #frame { background: %2; border: 1px solid %3; border-radius: 14px; }
        SendCoinsDialog #scrollArea, SendCoinsDialog #scrollAreaWidgetContents { background: transparent; border: none; }
        SendCoinsDialog #labelBalance { color: #7c3aed; font-weight: 700; }
        SendCoinsEntry { background: %2; border: 1px solid %3; border-radius: 14px; }
        SendCoinsEntry #line { background: transparent; border: none; max-height: 0px; }

        /* Fields and buttons */
        QLineEdit { background: %2; color: %4; border: 1px solid %3; border-radius: 8px; padding: 5px 8px;
            selection-background-color: #8b5cf6; selection-color: white; }
        QLineEdit:focus { border: 1px solid #8b5cf6; }
        QPushButton { background: %2; color: %4; border: 1px solid %3; border-radius: 9px; padding: 6px 14px; }
        QPushButton:hover { border: 1px solid #8b5cf6; }
        QPushButton:pressed { background: %5; }
        QPushButton:disabled { color: %6; }
        #sendButton, #receiveButton { color: white; font-weight: 700; border: none; padding: 8px 20px;
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #22d3ee, stop:1 #8b5cf6); }
        #sendButton:hover, #receiveButton:hover { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #67e8f9, stop:1 #a78bfa); }
        #sendButton:disabled, #receiveButton:disabled { background: #94a3b8; }

        /* Tables (Transactions, Receive history) */
        QTableView { background: %2; alternate-background-color: %1; color: %4; border: 1px solid %3; border-radius: 12px;
            gridline-color: %2; selection-background-color: %5; selection-color: %4; }
        QHeaderView::section { background: %2; color: %6; font-weight: 600; border: none;
            border-bottom: 2px solid %3; padding: 7px 8px; }
    )").arg(c.page, c.surface, c.border, c.text, c.tint, c.muted));
}

void AddShadow(QWidget* widget, int blur, int alpha)
{
    auto* effect = new QGraphicsDropShadowEffect(widget);
    effect->setBlurRadius(blur);
    effect->setOffset(0, 4);
    effect->setColor(QColor(15, 23, 42, alpha));
    widget->setGraphicsEffect(effect);
}

} // namespace PQCHStyle
