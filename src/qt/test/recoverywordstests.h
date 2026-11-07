// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TEST_RECOVERYWORDSTESTS_H
#define BITCOIN_QT_TEST_RECOVERYWORDSTESTS_H

#include <QObject>
#include <QTest>

class RecoveryWordsTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void backupDialogTests();
    void restoreDialogTests();
};

#endif // BITCOIN_QT_TEST_RECOVERYWORDSTESTS_H
