// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_MININGPAGE_H
#define BITCOIN_QT_MININGPAGE_H

#include <QString>
#include <QWidget>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

class ClientModel;
class PlatformStyle;
class WalletModel;
class QCheckBox;
class QFrame;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;

/**
 * PQCH: the "Mining" tab. Mines with the node's built-in RandomX miner (the generatetoaddress RPC,
 * called in short rounds from a worker thread so that Stop answers within seconds) to a new P2MR
 * address of this wallet. Shows the miner's hash rate (getmininginfo "localhashps"), the network's,
 * this computer's share and the expected time between its blocks. One miner per process: only one
 * wallet tab can mine at a time.
 */
class MiningPage : public QWidget
{
    Q_OBJECT

public:
    explicit MiningPage(const PlatformStyle* platform_style, QWidget* parent = nullptr);
    ~MiningPage();

    void setClientModel(ClientModel* client_model);
    void setWalletModel(WalletModel* wallet_model);

    bool isMining() const { return m_thread.joinable(); }
    int blocksFound() const { return m_blocks_found; }

protected:
    void changeEvent(QEvent* e) override;

public Q_SLOTS:
    void startMining();
    void stopMining();

private:
    void workerLoop(std::string address, unsigned threads);
    void post(std::function<void()> fn);
    enum class Tone { Idle, Active, Waiting, Error };
    //! The pill in the header (a short word) and, under the title, the details.
    void setStatus(const QString& pill, Tone tone, const QString& detail = QString());
    void applyStyle();
    QLabel* addStat(class QGridLayout* grid, int column, const QString& caption, const QString& accent, QLabel** sub);
    void workerFinished();
    void maybeAutoStart();
    void refresh();
    void updateControls();

    ClientModel* m_client_model{nullptr};
    WalletModel* m_wallet_model{nullptr};

    QSpinBox* m_threads{nullptr};
    QCheckBox* m_fast{nullptr};
    QCheckBox* m_autostart{nullptr};
    bool m_autostarted{false};
    QPushButton* m_button{nullptr};
    QLabel* m_status{nullptr};
    QLabel* m_hashrate{nullptr};
    QLabel* m_network{nullptr};
    QLabel* m_share{nullptr};
    QLabel* m_found{nullptr};
    QLabel* m_address{nullptr};
    QLabel* m_status_detail{nullptr};
    QLabel* m_hashrate_sub{nullptr};
    QLabel* m_network_sub{nullptr};
    QLabel* m_share_sub{nullptr};
    QLabel* m_found_sub{nullptr};
    QFrame* m_warning{nullptr};
    Tone m_tone{Tone::Idle};
    QTimer* m_timer{nullptr};

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    int m_blocks_found{0};
    double m_network_hashps{0};
    int m_refresh_count{0};
    QString m_mining_address;
};

#endif // BITCOIN_QT_MININGPAGE_H
