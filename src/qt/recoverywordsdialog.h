// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_RECOVERYWORDSDIALOG_H
#define BITCOIN_QT_RECOVERYWORDSDIALOG_H

#include <support/allocators/secure.h>
#include <util/result.h>

#include <QDialog>

#include <array>
#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;

/** PQCH: shows a wallet's 24 recovery words.
 *
 * Backup mode (just after a wallet is created): the words, a warning, then a check that three
 * of them, chosen at random, were written down. Show mode (Settings menu): only the words. */
class RecoveryWordsDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Mode { Backup, Show };

    RecoveryWordsDialog(const SecureString& words, Mode mode, QWidget* parent);
    ~RecoveryWordsDialog();

    void reject() override;

private:
    void goToCheck();
    void check();

    const Mode m_mode;
    std::vector<SecureString> m_words;
    std::array<int, 3> m_asked{};
    QStackedWidget* m_pages{nullptr};
    QCheckBox* m_written{nullptr};
    QPushButton* m_next{nullptr};
    std::array<QLabel*, 3> m_ask_labels{};
    std::array<QLineEdit*, 3> m_ask_edits{};
    QLabel* m_check_error{nullptr};
};

/** PQCH: asks for a wallet name, the 24 recovery words and the optional BIP39 passphrase. */
class RestoreWordsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RestoreWordsDialog(QWidget* parent);
    ~RestoreWordsDialog();

    QString walletName() const;
    SecureString words() const;
    SecureString wordsPassphrase() const;
    bool encrypt() const;

    void accept() override;

private:
    QLineEdit* m_name{nullptr};
    QPlainTextEdit* m_words{nullptr};
    QLineEdit* m_passphrase{nullptr};
    QCheckBox* m_encrypt{nullptr};
    QLabel* m_error{nullptr};
};

/** PQCH: checks typed recovery words against the wallet. The words are never shown or stored. */
class VerifyWordsDialog : public QDialog
{
    Q_OBJECT

public:
    using Check = std::function<util::Result<bool>(const SecureString& words, const SecureString& passphrase)>;
    VerifyWordsDialog(Check check, QWidget* parent);
    ~VerifyWordsDialog();

private:
    void verify();

    Check m_check;
    QPlainTextEdit* m_words{nullptr};
    QLineEdit* m_passphrase{nullptr};
    QLabel* m_result{nullptr};
};

#endif // BITCOIN_QT_RECOVERYWORDSDIALOG_H
