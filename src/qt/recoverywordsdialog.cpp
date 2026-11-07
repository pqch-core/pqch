// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/recoverywordsdialog.h>

#include <qt/guiutil.h>
#include <random.h>
#include <script/bip39.h>
#include <support/cleanse.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
std::vector<SecureString> SplitWords(const SecureString& words)
{
    std::vector<SecureString> out;
    SecureString cur;
    for (char c : words) {
        if (c == ' ') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

QLabel* WarningLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    label->setStyleSheet("QLabel { background: #fff4e0; color: #5c3b00; border: 1px solid #f0b44c; border-radius: 6px; padding: 10px; }");
    return label;
}

//! Lowercase, single spaces: what the wallet expects.
SecureString CleanWords(const QString& text)
{
    const QByteArray bytes = text.simplified().toLower().toUtf8();
    SecureString out(bytes.constData(), bytes.size());
    return out;
}
} // namespace

RecoveryWordsDialog::RecoveryWordsDialog(const SecureString& words, Mode mode, QWidget* parent)
    : QDialog(parent, GUIUtil::dialog_flags), m_mode(mode), m_words(SplitWords(words))
{
    setWindowTitle(tr("Recovery words"));
    setModal(true);
    setMinimumWidth(520);
    auto* layout = new QVBoxLayout(this);
    m_pages = new QStackedWidget(this);
    layout->addWidget(m_pages);

    // Page 1: the words
    auto* show_page = new QWidget;
    auto* show_layout = new QVBoxLayout(show_page);
    auto* intro = new QLabel(mode == Mode::Backup
        ? tr("<b>Write down these 24 words, in order, on paper.</b> They are the only way to recover this wallet if this computer is lost, broken or stolen. <b>They are shown only this once:</b> after you confirm them, they are deleted from this computer.")
        : tr("These are the 24 recovery words of this wallet, in order."));
    intro->setWordWrap(true);
    show_layout->addWidget(intro);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(6);
    const int rows = (m_words.size() + 2) / 3;
    for (size_t i = 0; i < m_words.size(); ++i) {
        auto* num = new QLabel(QString("%1.").arg(i + 1));
        num->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        num->setStyleSheet("QLabel { color: palette(mid); }");
        auto* word = new QLabel(QString::fromStdString(std::string(m_words[i].begin(), m_words[i].end())));
        word->setTextInteractionFlags(Qt::NoTextInteraction); // no copy to the clipboard
        QFont f = GUIUtil::fixedPitchFont();
        f.setPointSizeF(f.pointSizeF() * 1.25);
        f.setBold(true);
        word->setFont(f);
        const int row = i % rows, col = (i / rows) * 2;
        grid->addWidget(num, row, col);
        grid->addWidget(word, row, col + 1);
    }
    show_layout->addLayout(grid);
    show_layout->addWidget(WarningLabel(tr(
        "<b>Keep them secret and offline.</b> Anyone with these words can take all the funds. "
        "Never type them into a website, never photograph them, never store them in email, "
        "chat or the cloud. PQCH will never ask for them.")));

    auto* show_buttons = new QHBoxLayout;
    show_buttons->addStretch();
    if (mode == Mode::Backup) {
        m_written = new QCheckBox(tr("I have written down the 24 words"));
        show_layout->addWidget(m_written);
        m_next = new QPushButton(tr("Next"));
        m_next->setEnabled(false);
        m_next->setDefault(true);
        connect(m_written, &QCheckBox::toggled, m_next, &QPushButton::setEnabled);
        connect(m_next, &QPushButton::clicked, this, &RecoveryWordsDialog::goToCheck);
        show_buttons->addWidget(m_next);
    } else {
        auto* close = new QPushButton(tr("Close"));
        close->setDefault(true);
        connect(close, &QPushButton::clicked, this, &QDialog::accept);
        show_buttons->addWidget(close);
    }
    show_layout->addLayout(show_buttons);
    m_pages->addWidget(show_page);

    if (mode == Mode::Backup) {
        // Page 2: check three words chosen at random
        auto* check_page = new QWidget;
        auto* check_layout = new QVBoxLayout(check_page);
        auto* check_intro = new QLabel(tr("To make sure the words were written down correctly, type the words asked below."));
        check_intro->setWordWrap(true);
        check_layout->addWidget(check_intro);
        auto* form = new QFormLayout;
        for (size_t k = 0; k < m_ask_edits.size(); ++k) {
            m_ask_labels[k] = new QLabel;
            m_ask_edits[k] = new QLineEdit;
            m_ask_edits[k]->setAutoFillBackground(true);
            form->addRow(m_ask_labels[k], m_ask_edits[k]);
        }
        check_layout->addLayout(form);
        m_check_error = new QLabel;
        m_check_error->setStyleSheet("QLabel { color: #c0392b; }");
        m_check_error->setWordWrap(true);
        check_layout->addWidget(m_check_error);
        check_layout->addStretch();
        auto* check_buttons = new QHBoxLayout;
        auto* back = new QPushButton(tr("Back to the words"));
        connect(back, &QPushButton::clicked, [this] { m_pages->setCurrentIndex(0); });
        auto* done = new QPushButton(tr("Confirm"));
        done->setDefault(true);
        connect(done, &QPushButton::clicked, this, &RecoveryWordsDialog::check);
        check_buttons->addWidget(back);
        check_buttons->addStretch();
        check_buttons->addWidget(done);
        check_layout->addLayout(check_buttons);
        m_pages->addWidget(check_page);
    }
}

RecoveryWordsDialog::~RecoveryWordsDialog()
{
    for (auto* edit : m_ask_edits) {
        if (edit) edit->clear();
    }
}

void RecoveryWordsDialog::goToCheck()
{
    // Three different positions, in increasing order.
    FastRandomContext rng;
    std::vector<int> positions(m_words.size());
    for (size_t i = 0; i < positions.size(); ++i) positions[i] = i;
    std::shuffle(positions.begin(), positions.end(), rng);
    std::sort(positions.begin(), positions.begin() + m_asked.size());
    for (size_t k = 0; k < m_asked.size(); ++k) {
        m_asked[k] = positions[k];
        m_ask_labels[k]->setText(tr("Word %1").arg(m_asked[k] + 1));
        m_ask_edits[k]->clear();
    }
    m_check_error->clear();
    m_pages->setCurrentIndex(1);
    m_ask_edits[0]->setFocus();
}

void RecoveryWordsDialog::check()
{
    bool ok = true;
    for (size_t k = 0; k < m_asked.size(); ++k) {
        const SecureString typed = CleanWords(m_ask_edits[k]->text());
        if (typed != m_words[m_asked[k]]) ok = false;
    }
    if (!ok) {
        m_check_error->setText(tr("At least one word is not right. Check what you wrote down, or go back to the words."));
        return;
    }
    QDialog::accept();
}

void RecoveryWordsDialog::reject()
{
    if (m_mode == Mode::Backup) {
        const auto answer = QMessageBox::warning(this, tr("Recovery words not confirmed"),
            tr("Without the recovery words, this wallet cannot be recovered if this computer is lost.<br><br>"
               "Until you confirm them, you can finish the backup later in <b>Settings → Recovery Words…</b>. Close anyway?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
    }
    QDialog::reject();
}

RestoreWordsDialog::RestoreWordsDialog(QWidget* parent)
    : QDialog(parent, GUIUtil::dialog_flags)
{
    setWindowTitle(tr("Restore wallet from recovery words"));
    setModal(true);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Type the 24 recovery words of the wallet, in order, separated by spaces. "
                                "The wallet then searches the whole blockchain for its transactions, which can take a while."));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    m_name = new QLineEdit;
    m_name->setPlaceholderText(tr("Restored wallet"));
    form->addRow(tr("Wallet name"), m_name);
    m_words = new QPlainTextEdit;
    m_words->setTabChangesFocus(true);
    m_words->setMinimumHeight(110);
    m_words->setFont(GUIUtil::fixedPitchFont());
    form->addRow(tr("Recovery words"), m_words);
    m_passphrase = new QLineEdit;
    m_passphrase->setEchoMode(QLineEdit::Password);
    m_passphrase->setPlaceholderText(tr("Leave empty unless one was used"));
    m_passphrase->setToolTip(tr("Optional BIP39 passphrase (\"25th word\"). A different passphrase gives a different wallet."));
    form->addRow(tr("Extra passphrase"), m_passphrase);
    layout->addLayout(form);

    m_encrypt = new QCheckBox(tr("Encrypt the restored wallet with a password (required)"));
    m_encrypt->setChecked(true);
    m_encrypt->setEnabled(false);
    layout->addWidget(m_encrypt);
    layout->addWidget(WarningLabel(tr("Only type the recovery words on a computer you trust. PQCH will never ask for them on a website.")));

    m_error = new QLabel;
    m_error->setStyleSheet("QLabel { color: #c0392b; }");
    m_error->setWordWrap(true);
    layout->addWidget(m_error);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Restore"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(560, sizeHint().height());
}

RestoreWordsDialog::~RestoreWordsDialog()
{
    m_words->clear();
    m_passphrase->clear();
}

QString RestoreWordsDialog::walletName() const
{
    const QString name = m_name->text().trimmed();
    return name.isEmpty() ? m_name->placeholderText() : name;
}

SecureString RestoreWordsDialog::words() const { return CleanWords(m_words->toPlainText()); }

SecureString RestoreWordsDialog::wordsPassphrase() const
{
    const QByteArray bytes = m_passphrase->text().toUtf8();
    return SecureString(bytes.constData(), bytes.size());
}

bool RestoreWordsDialog::encrypt() const { return m_encrypt->isChecked(); }

void RestoreWordsDialog::accept()
{
    const SecureString w = words();
    const auto list = SplitWords(w);
    if (list.size() != bip39::WORD_COUNT) {
        m_error->setText(tr("There must be 24 words (%1 typed).").arg(list.size()));
        return;
    }
    if (!bip39::EntropyFromMnemonic(std::string_view{w.data(), w.size()})) {
        m_error->setText(tr("These words are not valid recovery words. Check for a misspelled word or a wrong order."));
        return;
    }
    const SecureString p = wordsPassphrase();
    if (!bip39::IsValidPassphrase(std::string_view{p.data(), p.size()})) {
        m_error->setText(tr("The extra passphrase may only use letters, digits, spaces and common symbols (no accents)."));
        return;
    }
    QDialog::accept();
}

VerifyWordsDialog::VerifyWordsDialog(Check check, QWidget* parent)
    : QDialog(parent, GUIUtil::dialog_flags), m_check(std::move(check))
{
    setWindowTitle(tr("Check recovery words"));
    setModal(true);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Type the 24 words of your paper copy to check that they belong to this wallet. "
                                "For your safety, this wallet no longer keeps them and can never show them again."));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* form = new QFormLayout;
    m_words = new QPlainTextEdit;
    m_words->setTabChangesFocus(true);
    m_words->setMinimumHeight(110);
    m_words->setFont(GUIUtil::fixedPitchFont());
    form->addRow(tr("Recovery words"), m_words);
    m_passphrase = new QLineEdit;
    m_passphrase->setEchoMode(QLineEdit::Password);
    m_passphrase->setPlaceholderText(tr("Leave empty unless one was used"));
    form->addRow(tr("Extra passphrase"), m_passphrase);
    layout->addLayout(form);
    m_result = new QLabel;
    m_result->setWordWrap(true);
    layout->addWidget(m_result);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* check_button = buttons->addButton(tr("Check"), QDialogButtonBox::ActionRole);
    check_button->setDefault(true);
    connect(check_button, &QPushButton::clicked, this, &VerifyWordsDialog::verify);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(560, sizeHint().height());
}

VerifyWordsDialog::~VerifyWordsDialog()
{
    m_words->clear();
    m_passphrase->clear();
}

void VerifyWordsDialog::verify()
{
    const SecureString words{CleanWords(m_words->toPlainText())};
    const QByteArray pass = m_passphrase->text().toUtf8();
    const auto res{m_check(words, SecureString(pass.constData(), pass.size()))};
    if (!res) {
        m_result->setStyleSheet("QLabel { color: #c0392b; }");
        m_result->setText(tr("These are not valid recovery words. Check for a misspelled word or a wrong order."));
    } else if (*res) {
        m_result->setStyleSheet("QLabel { color: #1e8449; font-weight: bold; }");
        m_result->setText(tr("Correct: these words belong to this wallet. Keep the paper copy safe."));
    } else {
        m_result->setStyleSheet("QLabel { color: #c0392b; }");
        m_result->setText(tr("These words are valid, but they do not belong to this wallet (or the extra passphrase is different)."));
    }
}
