// Copyright (c) 2026 The PQCH developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/miningpage.h>

#include <chainparams.h>
#include <interfaces/node.h>
#include <net.h>
#include <outputtype.h>
#include <pow_randomx.h>
#include <qt/addresstablemodel.h>
#include <qt/clientmodel.h>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>
#include <univalue.h>
#include <util/chaintype.h>

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <functional>
#include <string>

namespace {
//! Only one miner per process: the node has one RandomX miner, shared by all wallets.
std::atomic<bool> g_gui_mining{false};

//! Each generatetoaddress call searches for about this long, so Stop answers within seconds.
constexpr double ROUND_SECONDS{5};

QString FormatRate(double hps)
{
    if (hps >= 1e6) return QObject::tr("%1 MH/s").arg(hps / 1e6, 0, 'f', 2);
    if (hps >= 1e3) return QObject::tr("%1 kH/s").arg(hps / 1e3, 0, 'f', 2);
    return QObject::tr("%1 H/s").arg(hps, 0, 'f', 0);
}

QString FormatDuration(double seconds)
{
    if (seconds < 120) return QObject::tr("%1 seconds").arg(seconds, 0, 'f', 0);
    if (seconds < 7200) return QObject::tr("%1 minutes").arg(seconds / 60, 0, 'f', 0);
    if (seconds < 2 * 86400) return QObject::tr("%1 hours").arg(seconds / 3600, 0, 'f', 1);
    return QObject::tr("%1 days").arg(seconds / 86400, 0, 'f', 1);
}
} // namespace

MiningPage::MiningPage(const PlatformStyle* platform_style, QWidget* parent) : QWidget(parent)
{
    Q_UNUSED(platform_style);
    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(tr("Mine PQCH with this computer's processor. Mined coins go to a new address of this wallet."));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* warning = new QLabel(tr("<b>Before you start:</b> mining runs the processor at full power. "
                                  "The computer gets hot, the fans get loud, it uses more electricity and a laptop's battery runs down quickly. "
                                  "On an ordinary computer the rewards are small and irregular. It is your choice."));
    warning->setWordWrap(true);
    warning->setStyleSheet(QStringLiteral("QLabel { background-color: rgba(251, 191, 106, 40); border: 1px solid rgba(251, 191, 106, 140); border-radius: 6px; padding: 8px; }"));
    layout->addWidget(warning);

    auto* settings_box = new QGroupBox(tr("Settings"));
    auto* form = new QFormLayout(settings_box);
    const int cores{static_cast<int>(std::max(1u, std::thread::hardware_concurrency()))};
    m_threads = new QSpinBox;
    m_threads->setRange(1, cores);
    m_threads->setMaximumWidth(90);
    m_threads->setToolTip(tr("How many processor threads mine. Fewer threads leave the computer free for other work and keep it cooler."));
    m_fast = new QCheckBox(tr("Fast mode (uses about 2.5 GB of memory)"));
    m_fast->setToolTip(tr("Several times faster. Takes up to a minute to prepare when mining starts. Leave it off on computers with less than 8 GB of memory."));
    QSettings settings;
    m_threads->setValue(std::clamp(settings.value("MiningThreads", std::max(1, cores / 2)).toInt(), 1, cores));
    m_fast->setChecked(settings.value("MiningFastMode", false).toBool());
    form->addRow(tr("Threads:"), m_threads);
    form->addRow(QString(), m_fast);
    layout->addWidget(settings_box);

    m_button = new QPushButton(tr("Start mining"));
    m_button->setMinimumHeight(36);
    connect(m_button, &QPushButton::clicked, this, [this] { isMining() ? stopMining() : startMining(); });
    layout->addWidget(m_button);

    auto* status_box = new QGroupBox(tr("Status"));
    auto* status = new QFormLayout(status_box);
    m_status = new QLabel(tr("Stopped"));
    m_hashrate = new QLabel(QStringLiteral("—"));
    m_network = new QLabel(QStringLiteral("—"));
    m_share = new QLabel(QStringLiteral("—"));
    m_found = new QLabel(QStringLiteral("0"));
    m_address = new QLabel(QStringLiteral("—"));
    m_address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_address->setWordWrap(true);
    status->addRow(tr("State:"), m_status);
    status->addRow(tr("Your hash rate:"), m_hashrate);
    status->addRow(tr("Network hash rate:"), m_network);
    status->addRow(tr("Your share:"), m_share);
    status->addRow(tr("Blocks found (this session):"), m_found);
    status->addRow(tr("Mining to:"), m_address);
    layout->addWidget(status_box);

    auto* note = new QLabel(tr("Mined coins appear under Transactions and can be spent after 720 blocks (about 12 hours)."));
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch(1);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MiningPage::refresh);
    m_timer->start(2000);
}

MiningPage::~MiningPage()
{
    m_stop = true;
    if (m_thread.joinable()) {
        m_thread.join();
        g_gui_mining = false;
    }
}

void MiningPage::setClientModel(ClientModel* client_model)
{
    m_client_model = client_model;
    updateControls();
}

void MiningPage::setWalletModel(WalletModel* wallet_model)
{
    m_wallet_model = wallet_model;
    updateControls();
}

void MiningPage::post(std::function<void()> fn)
{
    // Runs on the GUI thread; dropped by Qt if this page has been destroyed meanwhile.
    QMetaObject::invokeMethod(this, std::move(fn), Qt::QueuedConnection);
}

void MiningPage::setStatus(const QString& text)
{
    m_status->setText(text);
}

void MiningPage::startMining()
{
    if (isMining() || !m_client_model || !m_wallet_model) return;
    bool expected{false};
    if (!g_gui_mining.compare_exchange_strong(expected, true)) {
        QMessageBox::information(this, tr("Mining"), tr("Another wallet is already mining. Stop it there first."));
        return;
    }
    // A new P2MR address of this wallet (asks for the passphrase if the wallet is locked).
    const QString address{m_wallet_model->getAddressTableModel()->addRow(AddressTableModel::Receive, tr("Mining"), QString(), OutputType::BECH32M)};
    if (address.isEmpty()) {
        g_gui_mining = false;
        QMessageBox::critical(this, tr("Mining"), tr("Could not get a new address from the wallet. If it is encrypted, unlock it and try again."));
        return;
    }
    m_mining_address = address;
    m_address->setText(address);

    const unsigned threads{static_cast<unsigned>(m_threads->value())};
    QSettings settings;
    settings.setValue("MiningThreads", m_threads->value());
    settings.setValue("MiningFastMode", m_fast->isChecked());
    g_randomx_mining_threads = threads;
    g_randomx_fast_mining = m_fast->isChecked();

    m_stop = false;
    setStatus(m_fast->isChecked() ? tr("Starting (preparing fast mode, up to a minute)…") : tr("Starting…"));
    m_thread = std::thread([this, address = address.toStdString(), threads] { workerLoop(address, threads); });
    updateControls();
}

void MiningPage::stopMining()
{
    if (!isMining()) return;
    m_stop = true;
    setStatus(tr("Stopping…"));
    m_button->setEnabled(false);
}

void MiningPage::workerLoop(std::string address, unsigned threads)
{
    interfaces::Node& node{m_client_model->node()};
    const bool regtest{Params().GetChainType() == ChainType::REGTEST};
    auto pause{[this](std::chrono::seconds s) {
        for (auto waited{std::chrono::milliseconds{0}}; waited < s && !m_stop; waited += std::chrono::milliseconds{250}) {
            std::this_thread::sleep_for(std::chrono::milliseconds{250});
        }
    }};
    bool announced_mining{false};
    while (!m_stop) {
        try {
            // Mining before the node has caught up, or with no peers, only makes orphans.
            if (!regtest && (node.isInitialBlockDownload() || node.getNodeCount(ConnectionDirection::Both) == 0)) {
                post([this] { setStatus(tr("Waiting for the node to connect and catch up…")); });
                announced_mining = false;
                pause(std::chrono::seconds{5});
                continue;
            }
            // Rounds of about ROUND_SECONDS; until the first measurement, a small one.
            const double rate{RandomXMiningHashrate()};
            const uint64_t tries{rate > 0 ? std::clamp<uint64_t>(static_cast<uint64_t>(rate * ROUND_SECONDS), 16ULL * threads, 50'000'000ULL)
                                          : 16ULL * threads};
            UniValue params(UniValue::VARR);
            params.push_back(1);
            params.push_back(address);
            params.push_back(tries);
            const UniValue result{node.executeRpc("generatetoaddress", params, "")};
            if (!announced_mining && !m_stop) {
                post([this] { setStatus(tr("Mining")); });
                announced_mining = true;
            }
            if (result.isArray() && !result.empty()) {
                const QString hash{QString::fromStdString(result[0].get_str())};
                post([this, hash] {
                    ++m_blocks_found;
                    m_found->setText(QString::number(m_blocks_found));
                    m_found->setToolTip(tr("Last block: %1").arg(hash));
                });
            }
        } catch (const UniValue& e) {
            const QString msg{QString::fromStdString(e.find_value("message").isStr() ? e.find_value("message").get_str() : e.write())};
            post([this, msg] { setStatus(tr("Error: %1 (retrying in 10 seconds)").arg(msg)); });
            announced_mining = false;
            pause(std::chrono::seconds{10});
        } catch (const std::exception& e) {
            const QString msg{QString::fromStdString(e.what())};
            post([this, msg] { setStatus(tr("Error: %1 (retrying in 10 seconds)").arg(msg)); });
            announced_mining = false;
            pause(std::chrono::seconds{10});
        }
    }
    post([this] { workerFinished(); });
}

void MiningPage::workerFinished()
{
    if (m_thread.joinable()) m_thread.join();
    g_gui_mining = false;
    setStatus(tr("Stopped"));
    updateControls();
}

void MiningPage::updateControls()
{
    const bool mining{isMining()};
    m_button->setText(mining ? tr("Stop mining") : tr("Start mining"));
    m_button->setEnabled(m_client_model && m_wallet_model && !(mining && m_stop));
    m_threads->setEnabled(!mining);
    m_fast->setEnabled(!mining);
}

void MiningPage::refresh()
{
    const double local{RandomXMiningHashrate()};
    m_hashrate->setText(isMining() || local > 0 ? FormatRate(local) : QStringLiteral("—"));
    if (!m_client_model || !isVisible()) return;
    // The network's rate changes slowly: ask the node every 10 seconds.
    if (m_refresh_count++ % 5 == 0) {
        try {
            const UniValue r{m_client_model->node().executeRpc("getnetworkhashps", UniValue(UniValue::VARR), "")};
            if (r.isNum()) m_network_hashps = r.get_real();
        } catch (...) {
        }
    }
    m_network->setText(m_network_hashps > 0 ? FormatRate(m_network_hashps) : QStringLiteral("—"));
    if (local > 0 && m_network_hashps > 0) {
        // The network estimate already counts this computer's work, so it is the total.
        const double share{std::min(1.0, local / std::max(m_network_hashps, local))};
        const double spacing{static_cast<double>(Params().GetConsensus().nPowTargetSpacing)};
        m_share->setText(tr("%1% of the network: on average one block every %2")
                             .arg(share * 100, 0, 'f', share < 0.01 ? 2 : 1)
                             .arg(FormatDuration(spacing / share)));
    } else {
        m_share->setText(QStringLiteral("—"));
    }
}
