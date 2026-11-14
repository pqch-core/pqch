// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_PQCHSTYLE_H
#define BITCOIN_QT_PQCHSTYLE_H

#include <QString>

class QWidget;

/**
 * PQCH: the wallet's look, in the brand colours of pqch.org (cyan, violet, orange, pink on night blue):
 * a soft page background, white rounded cards with shadows, a gradient balance card, gradient primary
 * buttons and a tab bar with a tinted selected tab. Light and dark system palettes.
 */
namespace PQCHStyle {
//! True when the system palette is dark.
bool IsDark();
//! Style sheet for the tabs toolbar.
QString ToolbarSheet();
//! Page background and style sheet for the wallet's pages (Overview, Send, Receive, Transactions).
void ApplyWalletStyle(QWidget* wallet_view);
//! A soft drop shadow under a card.
void AddShadow(QWidget* widget, int blur = 28, int alpha = 40);
} // namespace PQCHStyle

#endif // BITCOIN_QT_PQCHSTYLE_H
