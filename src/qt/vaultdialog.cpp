// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/vaultdialog.h>

#include <interfaces/node.h>
#include <qt/guiutil.h>
#include <qt/walletmodel.h>
#include <univalue.h>

#include <QApplication>
#include <QClipboard>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int MAX_OLDER{0xFFFF}; // p2mr::MAX_VAULT_OLDER

QLabel* Note(const QString& text, bool warning = false)
{
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    if (warning) label->setStyleSheet("QLabel { background: #fff4e0; color: #5c3b00; border: 1px solid #f0b44c; border-radius: 6px; padding: 8px; }");
    return label;
}

//! "1440 blocks (about 1 day)": blocks are one minute apart on average.
QString Delay(int blocks)
{
    if (blocks == 0) return QObject::tr("none (at any time)");
    const int minutes{blocks};
    QString about;
    if (minutes < 120) about = QObject::tr("about %n minute(s)", "", minutes);
    else if (minutes < 24 * 60) about = QObject::tr("about %n hour(s)", "", minutes / 60);
    else about = QObject::tr("about %n day(s)", "", minutes / (24 * 60));
    return QObject::tr("%1 blocks (%2)").arg(blocks).arg(about);
}
} // namespace

VaultDialog::VaultDialog(WalletModel* model, QWidget* parent)
    : QDialog(parent, GUIUtil::dialog_flags), m_model(model)
{
    // The wallet's name in the title: the steps happen in two wallets, and doing one in the wrong
    // wallet is the easiest mistake to make.
    const QString wallet{model->getWalletName().isEmpty() ? tr("[default wallet]") : model->getWalletName()};
    setWindowTitle(tr("Agent vaults — wallet %1").arg(wallet));
    setMinimumSize(760, 520);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(Note(tr("A <b>vault</b> holds the coins of an agent (a program that pays by itself). The agent's wallet spends them at any time; "
                              "the owner's wallet can take them back with its own post-quantum recovery key, after a delay you choose "
                              "(or at any time, with no delay). A delay does not stop a thief who has the agent's keys: it only decides when the owner can recover.")));
    auto* tabs = new QTabWidget;
    layout->addWidget(tabs);

    // Vaults
    {
        auto* page = new QWidget;
        auto* v = new QVBoxLayout(page);
        m_table = new QTableWidget(0, 6);
        m_table->setHorizontalHeaderLabels({tr("Role"), tr("Addresses"), tr("Owner's delay"), tr("Amount"), tr("Recoverable now"), tr("Next recoverable in")});
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setStretchLastSection(true);
        m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_table->setSelectionMode(QAbstractItemView::NoSelection);
        m_table->verticalHeader()->hide();
        v->addWidget(m_table);
        m_status = new QLabel;
        m_status->setWordWrap(true);
        v->addWidget(m_status);
        auto* row = new QHBoxLayout;
        m_recover_to = new QLineEdit;
        m_recover_to->setPlaceholderText(tr("Address for the recovered coins (empty: a new address of this wallet)"));
        row->addWidget(m_recover_to, 1);
        m_fee_rate = new QDoubleSpinBox;
        m_fee_rate->setRange(0, 10000);
        m_fee_rate->setDecimals(1);
        m_fee_rate->setSuffix(tr(" quanta/vB"));
        m_fee_rate->setSpecialValueText(tr("Automatic fee"));
        m_fee_rate->setToolTip(tr("Fee rate of the recovery. Automatic: the node's estimate, or 10 quanta/vB if it has none."));
        row->addWidget(m_fee_rate);
        auto* recover = new QPushButton(tr("Recover now"));
        recover->setToolTip(tr("Owner: send every vault coin that can be recovered to the address, through the recovery key"));
        row->addWidget(recover);
        auto* refresh = new QPushButton(tr("Refresh"));
        row->addWidget(refresh);
        v->addLayout(row);
        connect(recover, &QPushButton::clicked, this, &VaultDialog::recover);
        connect(refresh, &QPushButton::clicked, this, &VaultDialog::refresh);
        tabs->addTab(page, tr("Vaults"));
    }

    // Agent
    {
        auto* page = new QWidget;
        auto* v = new QVBoxLayout(page);
        v->addWidget(Note(tr("<b>In the agent's wallet.</b> Paste the owner's recovery key (tab Owner, in the owner's wallet), choose the delay and make the vault. "
                             "The vault then gives this wallet's new receiving and change addresses.")));
        auto* form = new QFormLayout;
        m_owner_key = new QLineEdit;
        m_owner_key->setPlaceholderText("slh([...]...)");
        form->addRow(tr("Owner's recovery key"), m_owner_key);
        m_older = new QSpinBox;
        m_older->setRange(0, MAX_OLDER);
        m_older->setValue(1440);
        m_older->setSuffix(tr(" blocks"));
        auto* older_note = new QLabel(Delay(1440));
        connect(m_older, qOverload<int>(&QSpinBox::valueChanged), older_note, [older_note](int n) { older_note->setText(Delay(n)); });
        auto* older_row = new QHBoxLayout;
        older_row->addWidget(m_older);
        older_row->addWidget(older_note, 1);
        form->addRow(tr("Owner's delay"), older_row);
        m_vault_index = new QSpinBox;
        m_vault_index->setRange(0, 1000000);
        form->addRow(tr("Vault number"), m_vault_index);
        v->addLayout(form);
        auto* create = new QPushButton(tr("Make the vault"));
        v->addWidget(create, 0, Qt::AlignLeft);
        v->addWidget(Note(tr("<b>Give the text below to the owner over a private channel.</b> It holds the vault's seed: whoever has it can spend the vault "
                             "like the agent. The owner imports it in tab Owner."), /*warning=*/true));
        m_import_out = new QPlainTextEdit;
        m_import_out->setReadOnly(true);
        v->addWidget(m_import_out, 1);
        auto* buttons = new QHBoxLayout;
        auto* copy = new QPushButton(tr("Copy"));
        copy->setToolTip(tr("Copies the text without showing it"));
        buttons->addWidget(copy);
        m_show_import = new QPushButton(tr("Show"));
        m_show_import->setCheckable(true);
        m_show_import->setEnabled(false);
        buttons->addWidget(m_show_import);
        buttons->addStretch(1);
        v->addLayout(buttons);
        connect(create, &QPushButton::clicked, this, &VaultDialog::createVault);
        connect(copy, &QPushButton::clicked, this, [this] {
            if (!m_owner_import.isEmpty()) QApplication::clipboard()->setText(m_owner_import);
        });
        connect(m_show_import, &QPushButton::toggled, this, &VaultDialog::showOwnerImport);
        tabs->addTab(page, tr("Agent"));
    }

    // Owner
    {
        auto* page = new QWidget;
        auto* v = new QVBoxLayout(page);
        v->addWidget(Note(tr("<b>In the owner's wallet.</b> 1. Show the recovery key and give it to the agent's wallet (it is public). "
                             "2. Paste here what the agent's wallet gives back, and import it. Use a different key number for each vault.")));
        auto* key_row = new QHBoxLayout;
        key_row->addWidget(new QLabel(tr("Key number")));
        m_key_index = new QSpinBox;
        m_key_index->setRange(0, 1000000);
        key_row->addWidget(m_key_index);
        auto* show = new QPushButton(tr("Show recovery key"));
        key_row->addWidget(show);
        key_row->addStretch(1);
        v->addLayout(key_row);
        auto* out_row = new QHBoxLayout;
        m_key_out = new QLineEdit;
        m_key_out->setReadOnly(true);
        out_row->addWidget(m_key_out, 1);
        auto* copy = new QPushButton(tr("Copy"));
        out_row->addWidget(copy);
        v->addLayout(out_row);
        v->addWidget(new QLabel(tr("Vault from the agent's wallet:")));
        m_import_in = new QPlainTextEdit;
        m_import_in->setPlaceholderText("[{\"desc\": \"p2mr_vault(...)\", ...}]");
        v->addWidget(m_import_in, 1);
        auto* import = new QPushButton(tr("Import the vault"));
        v->addWidget(import, 0, Qt::AlignLeft);
        connect(show, &QPushButton::clicked, this, &VaultDialog::showKey);
        connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_key_out->text()); });
        connect(import, &QPushButton::clicked, this, &VaultDialog::importVault);
        tabs->addTab(page, tr("Owner"));
    }

    refresh();
}

std::optional<UniValue> VaultDialog::Call(const std::string& method, const UniValue& params, bool unlock)
{
    const auto run{[&]() -> std::optional<UniValue> {
        const QByteArray name{QUrl::toPercentEncoding(m_model->getWalletName())};
        try {
            return m_model->node().executeRpc(method, params, "/wallet/" + std::string(name.constData(), name.size()));
        } catch (const UniValue& e) {
            const UniValue& msg{e.find_value("message")};
            QMessageBox::warning(this, windowTitle(), QString::fromStdString(msg.isStr() ? msg.get_str() : e.write()));
        } catch (const std::exception& e) {
            QMessageBox::warning(this, windowTitle(), QString::fromStdString(e.what()));
        }
        return std::nullopt;
    }};
    if (!unlock) return run();
    WalletModel::UnlockContext ctx(m_model->requestUnlock());
    if (!ctx.isValid()) return std::nullopt;
    return run();
}

void VaultDialog::refresh()
{
    const auto res{Call("listvaults", UniValue{UniValue::VARR}, /*unlock=*/false)};
    m_table->setRowCount(0);
    if (!res) return;
    bool owner{false};
    for (const UniValue& v : res->getValues()) {
        const int row{m_table->rowCount()};
        m_table->insertRow(row);
        const bool is_owner{v["role"].get_str() == "owner"};
        owner |= is_owner;
        const auto set{[&](int col, const QString& text) { m_table->setItem(row, col, new QTableWidgetItem(text)); }};
        set(0, is_owner ? tr("Owner") : tr("Agent"));
        QString addresses{v["internal"].get_bool() ? tr("change") : tr("receiving")};
        if (v["active"].get_bool()) addresses += tr(", in use");
        set(1, addresses);
        set(2, Delay(v["older"].getInt<int>()));
        QString amount{QString::fromStdString(v["amount"].getValStr()) + " PQCH"};
        if (v.exists("unconfirmed") && v["unconfirmed"].getValStr() != "0.00000000") {
            amount += " " + tr("(%1 not yet confirmed)").arg(QString::fromStdString(v["unconfirmed"].getValStr()));
        }
        set(3, amount);
        set(4, is_owner ? QString::fromStdString(v["recoverable"].getValStr()) + " PQCH" : QString("-"));
        set(5, is_owner && v.exists("next_recoverable_in") ? Delay(v["next_recoverable_in"].getInt<int>()) : QString("-"));
    }
    if (res->empty()) m_status->setText(tr("This wallet has no vault yet."));
    else m_status->setText(owner ? tr("As owner, this wallet cannot spend the vaults' coins normally: it only recovers them.") : QString());
}

void VaultDialog::recover()
{
    if (QMessageBox::question(this, windowTitle(), tr("Send every vault coin that can be recovered now to %1?")
                              .arg(m_recover_to->text().trimmed().isEmpty() ? tr("a new address of this wallet") : m_recover_to->text().trimmed())) != QMessageBox::Yes) return;
    UniValue params{UniValue::VARR};
    const QString to{m_recover_to->text().trimmed()};
    params.push_back(to.isEmpty() ? UniValue{} : UniValue{to.toStdString()});
    if (m_fee_rate->value() > 0) params.push_back(UniValue{UniValue::VNUM, QString::number(m_fee_rate->value(), 'f', 1).toStdString()});
    if (const auto res{Call("recovervault", params, /*unlock=*/true)}) {
        const std::string source{(*res)["fee_source"].getValStr()};
        const QString how{source == "given" ? tr("the rate you chose") : source == "estimated" ? tr("the node's estimate") : tr("the default rate, since the node has no estimate")};
        QString text{tr("Recovered %1 PQCH from %n coin(s) (fee %2 PQCH, %3 quanta/vB: %4).", "", (*res)["inputs"].getInt<int>())
                     .arg(QString::fromStdString((*res)["amount"].getValStr()), QString::fromStdString((*res)["fee"].getValStr()),
                          QString::fromStdString((*res)["fee_rate"].getValStr()), how)};
        if (!(*res)["waiting"].empty()) text += " " + tr("%n coin(s) must wait longer.", "", static_cast<int>((*res)["waiting"].size()));
        QMessageBox::information(this, windowTitle(), text + "\n" + QString::fromStdString((*res)["txid"].get_str()));
    }
    refresh();
}

void VaultDialog::showOwnerImport(bool show)
{
    m_show_import->setText(show ? tr("Hide") : tr("Show"));
    if (m_owner_import.isEmpty()) {
        m_import_out->clear();
    } else if (show) {
        m_import_out->setPlainText(m_owner_import);
    } else {
        m_import_out->setPlainText(tr("Hidden: this text holds the vault's seed. Use Copy to pass it on without showing it, "
                                      "or Show if you must read it (do not photograph it)."));
    }
}

void VaultDialog::showKey()
{
    UniValue params{UniValue::VARR};
    params.push_back(m_key_index->value());
    if (const auto res{Call("getvaultkey", params, /*unlock=*/true)}) m_key_out->setText(QString::fromStdString((*res)["key"].get_str()));
}

void VaultDialog::createVault()
{
    const QString key{m_owner_key->text().trimmed()};
    if (key.isEmpty()) return;
    if (QMessageBox::question(this, windowTitle(), tr("Make vault number %1, with the owner's delay %2? The vault will give this wallet's new addresses.")
                              .arg(m_vault_index->value()).arg(Delay(m_older->value()))) != QMessageBox::Yes) return;
    UniValue params{UniValue::VARR};
    params.push_back(key.toStdString());
    params.push_back(m_older->value());
    params.push_back(m_vault_index->value());
    if (const auto res{Call("createvault", params, /*unlock=*/true)}) {
        m_owner_import = QString::fromStdString((*res)["owner_import"].write());
        m_show_import->setEnabled(true);
        m_show_import->setChecked(false);
        showOwnerImport(false);
        refresh();
    }
}

void VaultDialog::importVault()
{
    UniValue request;
    if (!request.read(m_import_in->toPlainText().trimmed().toStdString()) || !request.isArray()) {
        QMessageBox::warning(this, windowTitle(), tr("Paste exactly the text the agent's wallet gave (it starts with [ and ends with ])."));
        return;
    }
    for (const UniValue& item : request.getValues()) {
        if (!item.isObject() || !item["desc"].isStr() || !item["desc"].get_str().starts_with("p2mr_vault(")) {
            QMessageBox::warning(this, windowTitle(), tr("This is not a vault."));
            return;
        }
    }
    UniValue params{UniValue::VARR};
    params.push_back(request);
    if (const auto res{Call("importdescriptors", params, /*unlock=*/true)}) {
        for (const UniValue& r : res->getValues()) {
            if (!r["success"].get_bool()) {
                QMessageBox::warning(this, windowTitle(), QString::fromStdString(r["error"]["message"].getValStr()));
                return;
            }
        }
        m_import_in->clear();
        QMessageBox::information(this, windowTitle(), tr("Vault imported. Its coins appear in tab Vaults once the wallet has scanned the chain."));
        refresh();
    }
}
