// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_VAULTDIALOG_H
#define BITCOIN_QT_VAULTDIALOG_H

#include <QDialog>

#include <optional>

class UniValue;
class WalletModel;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

/**
 * PQCH: agent vaults (contrib/pqch/COFRE.md), on top of the vault RPCs (getvaultkey, createvault,
 * importdescriptors, listvaults, recovervault) of the current wallet.
 *   - "Vaults": the wallet's vaults, as agent or owner, and the owner's "Recover now".
 *   - "Agent": make a vault from the owner's key; copy the data the owner imports.
 *   - "Owner": show this wallet's recovery key for a vault; import a vault.
 */
class VaultDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VaultDialog(WalletModel* model, QWidget* parent = nullptr);

private:
    WalletModel* m_model;
    QTableWidget* m_table{nullptr};
    QLabel* m_status{nullptr};
    QLineEdit* m_recover_to{nullptr};
    QDoubleSpinBox* m_fee_rate{nullptr};
    QSpinBox* m_key_index{nullptr};
    QLineEdit* m_key_out{nullptr};
    QLineEdit* m_owner_key{nullptr};
    QSpinBox* m_older{nullptr};
    QSpinBox* m_vault_index{nullptr};
    QPlainTextEdit* m_import_out{nullptr};
    QPushButton* m_show_import{nullptr};
    //! What the owner imports (holds the vault's seed): shown only on request, so that it is not
    //! on the screen (or in a photo of it) by default.
    QString m_owner_import;
    void showOwnerImport(bool show);
    QPlainTextEdit* m_import_in{nullptr};

    /** Runs a wallet RPC; shows the error and returns nullopt if it fails. */
    std::optional<UniValue> Call(const std::string& method, const UniValue& params, bool unlock);
    void refresh();
    void recover();
    void showKey();
    void createVault();
    void importVault();
};

#endif // BITCOIN_QT_VAULTDIALOG_H
