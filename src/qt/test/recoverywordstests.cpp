// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/recoverywordstests.h>

#include <qt/recoverywordsdialog.h>
#include <util/translation.h>

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringList>

namespace {
// BIP39 vector: 32 zero bytes of entropy
const QStringList ZERO_WORDS = [] { QStringList l; for (int i = 0; i < 23; ++i) l << "abandon"; l << "art"; return l; }();

SecureString Secure(const QString& s)
{
    const QByteArray b = s.toUtf8();
    return SecureString(b.constData(), b.size());
}

QPushButton* Button(QWidget& w, const QString& text)
{
    for (auto* b : w.findChildren<QPushButton*>()) {
        if (b->text() == text) return b;
    }
    return nullptr;
}

//! Optional screenshots for review: PQCH_QT_SCREENSHOTS=<folder>
void Shot(QWidget& w, const char* name)
{
    const QByteArray dir = qgetenv("PQCH_QT_SCREENSHOTS");
    if (!dir.isEmpty()) w.grab().save(QString::fromUtf8(dir) + "/" + name + ".png");
}
} // namespace

void RecoveryWordsTests::backupDialogTests()
{
    RecoveryWordsDialog dlg(Secure(ZERO_WORDS.join(' ')), RecoveryWordsDialog::Mode::Backup, nullptr);
    dlg.show();
    Shot(dlg, "recovery-words");

    // All 24 words are shown, numbered.
    int shown = 0;
    for (auto* l : dlg.findChildren<QLabel*>()) {
        if (l->text() == "abandon" || l->text() == "art") ++shown;
    }
    QCOMPARE(shown, 24);

    // "Next" only after confirming the words were written down.
    QPushButton* next = Button(dlg, "Next");
    QVERIFY(next);
    QVERIFY(!next->isEnabled());
    dlg.findChild<QCheckBox*>()->setChecked(true);
    QVERIFY(next->isEnabled());
    next->click();

    // Three words asked; wrong answers are refused.
    QList<QLineEdit*> edits = dlg.findChildren<QLineEdit*>();
    QCOMPARE(edits.size(), 3);
    QList<int> asked;
    for (auto* l : dlg.findChildren<QLabel*>()) {
        auto m = QRegularExpression("^Word (\\d+)$").match(l->text());
        if (m.hasMatch()) asked << m.captured(1).toInt();
    }
    QCOMPARE(asked.size(), 3);
    QVERIFY(asked[0] < asked[1] && asked[1] < asked[2]);
    for (auto* e : edits) e->setText("bitcoin");
    Button(dlg, "Confirm")->click();
    QVERIFY(dlg.isVisible());
    Shot(dlg, "recovery-words-check");

    // Right answers (case and spaces do not matter) close the dialog.
    for (int k = 0; k < 3; ++k) edits[k]->setText(" " + ZERO_WORDS[asked[k] - 1].toUpper() + " ");
    Button(dlg, "Confirm")->click();
    QVERIFY(!dlg.isVisible());
    QCOMPARE(dlg.result(), int{QDialog::Accepted});
}

void RecoveryWordsTests::restoreDialogTests()
{
    RestoreWordsDialog dlg(nullptr);
    dlg.show();
    auto* words = dlg.findChild<QPlainTextEdit*>();
    QVERIFY(words);

    // Wrong number of words
    words->setPlainText("abandon abandon");
    dlg.accept();
    QVERIFY(dlg.isVisible());
    // Bad checksum
    words->setPlainText(QString("abandon ").repeated(24));
    dlg.accept();
    QVERIFY(dlg.isVisible());
    Shot(dlg, "restore-words");

    // Valid words, messy spacing and case: accepted and normalized
    words->setPlainText("  " + ZERO_WORDS.join("   ").toUpper() + "\n");
    dlg.accept();
    QVERIFY(!dlg.isVisible());
    QVERIFY(dlg.words() == Secure(ZERO_WORDS.join(' ')));
    QVERIFY(dlg.wordsPassphrase().empty());
    QVERIFY(dlg.encrypt());
    QVERIFY(!dlg.findChild<QCheckBox*>()->isEnabled()); // PQCH: encryption is not optional
    QCOMPARE(dlg.walletName(), QString("Restored wallet"));
}

void RecoveryWordsTests::verifyDialogTests()
{
    // The dialog only passes the typed words to the wallet's check and shows the answer.
    SecureString seen_words, seen_pass;
    VerifyWordsDialog dlg([&](const SecureString& words, const SecureString& pass) -> util::Result<bool> {
        seen_words = words;
        seen_pass = pass;
        if (words == Secure(ZERO_WORDS.join(' '))) return pass.empty();
        return util::Error{Untranslated("Invalid recovery words")};
    }, nullptr);
    dlg.show();
    auto* words = dlg.findChild<QPlainTextEdit*>();
    auto* pass = dlg.findChild<QLineEdit*>();
    QPushButton* check = Button(dlg, "Check");
    QVERIFY(words && pass && check);
    QLabel* result = nullptr;
    auto result_text = [&] {
        for (auto* l : dlg.findChildren<QLabel*>()) {
            if (l->text().startsWith("Correct") || l->text().startsWith("These")) return l->text();
        }
        return QString();
    };

    words->setPlainText(" " + ZERO_WORDS.join("  ").toUpper());
    check->click();
    QVERIFY(seen_words == Secure(ZERO_WORDS.join(' '))); // normalized before the check
    QVERIFY(result_text().startsWith("Correct"));
    Shot(dlg, "verify-words");

    pass->setText("other");
    check->click();
    QVERIFY(seen_pass == Secure("other"));
    QVERIFY(result_text().contains("do not belong"));

    words->setPlainText("abandon abandon");
    check->click();
    QVERIFY(result_text().contains("not valid"));
    (void)result;
}
