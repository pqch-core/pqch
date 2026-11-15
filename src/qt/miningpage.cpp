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

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QIcon>
#include <QScrollArea>
#include <QStyle>
#include <QTime>
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

namespace {
// PQCH brand colours (as on pqch.org).
const QString CYAN{QStringLiteral("#22d3ee")};
const QString VIOLET{QStringLiteral("#8b5cf6")};
const QString ORANGE{QStringLiteral("#f97316")};
const QString PINK{QStringLiteral("#db2777")};

QGraphicsDropShadowEffect* Shadow(QWidget* parent, int blur = 28, int alpha = 45)
{
    auto* effect = new QGraphicsDropShadowEffect(parent);
    effect->setBlurRadius(blur);
    effect->setOffset(0, 4);
    effect->setColor(QColor(15, 23, 42, alpha));
    return effect;
}
} // namespace

MiningPage::MiningPage(const PlatformStyle* platform_style, QWidget* parent) : QWidget(parent)
{
    Q_UNUSED(platform_style);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto* content = new QWidget;
    content->setObjectName("miningContent");
    scroll->setWidget(content);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(14);

    // Header: logo, title, state.
    auto* hero = new QFrame;
    hero->setObjectName("hero");
    hero->setGraphicsEffect(Shadow(hero, 34, 70));
    auto* hero_layout = new QHBoxLayout(hero);
    hero_layout->setContentsMargins(20, 16, 20, 16);
    hero_layout->setSpacing(16);
    auto* logo = new QLabel;
    logo->setPixmap(QIcon(":/icons/pqch_logo").pixmap(QSize(52, 52)));
    hero_layout->addWidget(logo);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(2);
    auto* title = new QLabel(tr("Mining"));
    title->setObjectName("heroTitle");
    m_status_detail = new QLabel;
    m_status_detail->setObjectName("heroText");
    m_status_detail->setWordWrap(true);
    titles->addWidget(title);
    titles->addWidget(m_status_detail);
    hero_layout->addLayout(titles, 1);
    m_status = new QLabel;
    m_status->setObjectName("pill");
    m_status->setAlignment(Qt::AlignCenter);
    hero_layout->addWidget(m_status, 0, Qt::AlignTop);
    layout->addWidget(hero);

    // Warning.
    m_warning = new QFrame;
    m_warning->setObjectName("warning");
    auto* warning_layout = new QHBoxLayout(m_warning);
    warning_layout->setContentsMargins(14, 10, 14, 10);
    auto* warning = new QLabel(tr("<b>Before you start:</b> mining runs the processor at full power. "
                                  "The computer gets hot, the fans get loud, it uses more electricity and a laptop's battery runs down quickly. "
                                  "On an ordinary computer the rewards are small and irregular. It is your choice."));
    warning->setObjectName("warningText");
    warning->setWordWrap(true);
    warning_layout->addWidget(warning);
    layout->addWidget(m_warning);

    // Settings and the Start / Stop button.
    auto* settings_card = new QFrame;
    settings_card->setObjectName("card");
    settings_card->setGraphicsEffect(Shadow(settings_card));
    auto* settings_layout = new QHBoxLayout(settings_card);
    settings_layout->setContentsMargins(18, 14, 18, 14);
    settings_layout->setSpacing(18);
    const int cores{static_cast<int>(std::max(1u, std::thread::hardware_concurrency()))};
    auto* threads_caption = new QLabel(tr("Threads"));
    threads_caption->setObjectName("caption");
    m_threads = new QSpinBox;
    m_threads->setRange(1, cores);
    m_threads->setMinimumWidth(70);
    m_threads->setToolTip(tr("How many processor threads mine. Fewer threads leave the computer free for other work and keep it cooler."));
    m_fast = new QCheckBox(tr("Fast mode (uses about 2.5 GB of memory)"));
    m_fast->setToolTip(tr("Several times faster. Takes up to a minute to prepare when mining starts. Leave it off on computers with less than 8 GB of memory."));
    QSettings settings;
    m_threads->setValue(std::clamp(settings.value("MiningThreads", std::max(1, cores / 2)).toInt(), 1, cores));
    m_fast->setChecked(settings.value("MiningFastMode", false).toBool());
    auto* threads_of = new QLabel(tr("of %1").arg(cores));
    threads_of->setObjectName("caption");
    settings_layout->addWidget(threads_caption);
    settings_layout->addWidget(m_threads);
    settings_layout->addWidget(threads_of);
    settings_layout->addSpacing(8);
    settings_layout->addWidget(m_fast);
    settings_layout->addStretch(1);
    m_button = new QPushButton(tr("Start mining"));
    m_button->setObjectName("startButton");
    m_button->setCursor(Qt::PointingHandCursor);
    m_button->setMinimumSize(190, 46);
    m_button->setGraphicsEffect(Shadow(m_button, 22, 60));
    connect(m_button, &QPushButton::clicked, this, [this] { isMining() ? stopMining() : startMining(); });
    settings_layout->addWidget(m_button);
    layout->addWidget(settings_card);

    // Figures.
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(14);
    m_hashrate = addStat(grid, 0, tr("Your hash rate"), CYAN, &m_hashrate_sub);
    m_network = addStat(grid, 1, tr("Network hash rate"), VIOLET, &m_network_sub);
    m_share = addStat(grid, 2, tr("Your share"), ORANGE, &m_share_sub);
    m_found = addStat(grid, 3, tr("Blocks found"), PINK, &m_found_sub);
    m_hashrate_sub->setText(tr("this computer"));
    m_network_sub->setText(tr("all miners"));
    m_share_sub->setText(tr("of the network"));
    m_found_sub->setText(tr("this session"));
    m_found->setText(QStringLiteral("0"));
    layout->addLayout(grid);

    // Payout address.
    auto* address_card = new QFrame;
    address_card->setObjectName("card");
    address_card->setGraphicsEffect(Shadow(address_card));
    auto* address_layout = new QVBoxLayout(address_card);
    address_layout->setContentsMargins(18, 12, 18, 12);
    address_layout->setSpacing(4);
    auto* address_caption = new QLabel(tr("MINING TO"));
    address_caption->setObjectName("caption");
    m_address = new QLabel(tr("A new address of this wallet, made when mining starts."));
    m_address->setObjectName("address");
    m_address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_address->setWordWrap(true);
    address_layout->addWidget(address_caption);
    address_layout->addWidget(m_address);
    layout->addWidget(address_card);

    const int maturity{Params().GetConsensus().nCoinbaseMaturity};
    const int maturity_minutes{static_cast<int>(maturity * Params().GetConsensus().nPowTargetSpacing / 60)};
    auto* note = new QLabel(tr("Mined coins appear under Transactions and can be spent after %1 blocks (about %2).")
                                .arg(maturity)
                                .arg(maturity_minutes >= 120 ? tr("%1 hours").arg(maturity_minutes / 60) : tr("%1 minutes").arg(maturity_minutes)));
    note->setObjectName("caption");
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch(1);

    applyStyle();
    setStatus(tr("Stopped"), Tone::Idle);
    updateControls();

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MiningPage::refresh);
    m_timer->start(2000);
}

QLabel* MiningPage::addStat(QGridLayout* grid, int column, const QString& caption, const QString& accent, QLabel** sub)
{
    auto* card = new QFrame;
    card->setObjectName("card");
    card->setGraphicsEffect(Shadow(card));
    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(0, 0, 14, 0);
    row->setSpacing(12);
    auto* bar = new QFrame;
    bar->setFixedWidth(5);
    bar->setStyleSheet(QStringLiteral("QFrame { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2); border-top-left-radius: 12px; border-bottom-left-radius: 12px; }")
                           .arg(accent, accent == PINK ? ORANGE : (accent == ORANGE ? PINK : (accent == CYAN ? VIOLET : CYAN))));
    row->addWidget(bar);
    auto* column_layout = new QVBoxLayout;
    column_layout->setContentsMargins(0, 12, 0, 12);
    column_layout->setSpacing(2);
    auto* caption_label = new QLabel(caption.toUpper());
    caption_label->setObjectName("caption");
    auto* value = new QLabel(QStringLiteral("—"));
    value->setObjectName("value");
    value->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(accent));
    *sub = new QLabel;
    (*sub)->setObjectName("caption");
    (*sub)->setWordWrap(true);
    column_layout->addWidget(caption_label);
    column_layout->addWidget(value);
    column_layout->addWidget(*sub);
    column_layout->addStretch(1);
    row->addLayout(column_layout, 1);
    grid->addWidget(card, 0, column);
    grid->setColumnStretch(column, 1);
    return value;
}

void MiningPage::applyStyle()
{
    static bool applying{false};
    if (applying) return;
    applying = true;
    const bool dark{QApplication::palette().color(QPalette::Window).lightness() < 128};
    const QString page{dark ? "#151827" : "#f4f5fb"};
    const QString surface{dark ? "#1f2337" : "#ffffff"};
    const QString border{dark ? "#2e3450" : "#e6e8f0"};
    const QString text{dark ? "#f1f5f9" : "#0f172a"};
    const QString muted{dark ? "#94a3b8" : "#64748b"};
    const QString warn_bg{dark ? "#3a2516" : "#fff7ed"};
    const QString warn_text{dark ? "#fed7aa" : "#7c2d12"};
    setStyleSheet(QStringLiteral(R"(
        #miningContent { background: %1; }
        QScrollArea { background: %1; }
        #hero { border-radius: 16px; background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #1e1b4b, stop:0.55 #3b2a6b, stop:1 #7c3aed); }
        #heroTitle { color: white; font-size: 22px; font-weight: 700; }
        #heroText { color: #cbd5e1; font-size: 13px; }
        #warning { background: %6; border: 1px solid rgba(249, 115, 22, 90); border-left: 5px solid #f97316; border-radius: 10px; }
        #warningText { color: %7; }
        #card { background: %2; border: 1px solid %3; border-radius: 12px; }
        #caption { color: %5; font-size: 11px; font-weight: 600; letter-spacing: 0.5px; }
        #value { font-size: 24px; font-weight: 700; }
        #address { color: %4; font-size: 13px; }
        #address[filled="true"] { font-family: monospace; font-size: 14px; font-weight: 600; }
        QCheckBox, QSpinBox { color: %4; }
        #startButton { color: white; font-size: 15px; font-weight: 700; border: none; border-radius: 12px; padding: 0 22px; }
        #startButton[mining="false"] { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #22d3ee, stop:1 #8b5cf6); }
        #startButton[mining="false"]:hover { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #67e8f9, stop:1 #a78bfa); }
        #startButton[mining="true"] { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #f97316, stop:1 #db2777); }
        #startButton[mining="true"]:hover { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #fb923c, stop:1 #ec4899); }
        #startButton:disabled { background: #94a3b8; }
    )").arg(page, surface, border, text, muted, warn_bg, warn_text));
    setStatus(m_status->text(), m_tone, m_status_detail->text());
    applying = false;
}

void MiningPage::changeEvent(QEvent* e)
{
    QWidget::changeEvent(e);
    if (e->type() == QEvent::PaletteChange && m_status) applyStyle();
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

void MiningPage::setStatus(const QString& pill, Tone tone, const QString& detail)
{
    m_tone = tone;
    const char* colour{"rgba(255, 255, 255, 40)"};
    if (tone == Tone::Active) colour = "#10b981";
    if (tone == Tone::Waiting) colour = "#f59e0b";
    if (tone == Tone::Error) colour = "#ef4444";
    m_status->setText(pill);
    m_status->setStyleSheet(QStringLiteral("QLabel { background: %1; color: white; border-radius: 10px; padding: 4px 14px; font-weight: 700; }").arg(QLatin1String(colour)));
    m_status_detail->setText(detail.isEmpty() ? tr("Mine PQCH with this computer's processor. Mined coins go to a new address of this wallet.") : detail);
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
    m_address->setProperty("filled", true);
    m_address->style()->unpolish(m_address);
    m_address->style()->polish(m_address);

    const unsigned threads{static_cast<unsigned>(m_threads->value())};
    QSettings settings;
    settings.setValue("MiningThreads", m_threads->value());
    settings.setValue("MiningFastMode", m_fast->isChecked());
    g_randomx_mining_threads = threads;
    g_randomx_fast_mining = m_fast->isChecked();

    m_stop = false;
    setStatus(tr("Starting"), Tone::Waiting, m_fast->isChecked() ? tr("Preparing fast mode, up to a minute…") : tr("Preparing the miner…"));
    m_thread = std::thread([this, address = address.toStdString(), threads] { workerLoop(address, threads); });
    updateControls();
}

void MiningPage::stopMining()
{
    if (!isMining()) return;
    m_stop = true;
    setStatus(tr("Stopping"), Tone::Waiting, tr("Finishing the current round, a few seconds…"));
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
                post([this] { setStatus(tr("Waiting"), Tone::Waiting, tr("Waiting for the node to connect and catch up…")); });
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
                post([this] { setStatus(tr("● Mining"), Tone::Active, (m_threads->value() == 1 ? tr("Searching for the next block with 1 thread.") : tr("Searching for the next block with %1 threads.").arg(m_threads->value()))); });
                announced_mining = true;
            }
            if (result.isArray() && !result.empty()) {
                const QString hash{QString::fromStdString(result[0].get_str())};
                post([this, hash] {
                    ++m_blocks_found;
                    m_found->setText(QString::number(m_blocks_found));
                    m_found->setToolTip(tr("Last block: %1").arg(hash));
                    m_found_sub->setText(tr("this session · last %1").arg(QTime::currentTime().toString("HH:mm")));
                });
            }
        } catch (const UniValue& e) {
            const QString msg{QString::fromStdString(e.find_value("message").isStr() ? e.find_value("message").get_str() : e.write())};
            post([this, msg] { setStatus(tr("Error"), Tone::Error, tr("%1 Retrying in 10 seconds.").arg(msg)); });
            announced_mining = false;
            pause(std::chrono::seconds{10});
        } catch (const std::exception& e) {
            const QString msg{QString::fromStdString(e.what())};
            post([this, msg] { setStatus(tr("Error"), Tone::Error, tr("%1 Retrying in 10 seconds.").arg(msg)); });
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
    setStatus(tr("Stopped"), Tone::Idle);
    updateControls();
}

void MiningPage::updateControls()
{
    const bool mining{isMining()};
    m_button->setText(mining ? tr("■  Stop mining") : tr("▶  Start mining"));
    m_button->setProperty("mining", mining);
    m_button->style()->unpolish(m_button);
    m_button->style()->polish(m_button);
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
        m_share->setText(QStringLiteral("%1%").arg(share * 100, 0, 'f', share < 0.01 ? 2 : 1));
        m_share_sub->setText(tr("on average one block every %1").arg(FormatDuration(spacing / share)));
    } else {
        m_share->setText(QStringLiteral("—"));
        m_share_sub->setText(tr("of the network"));
    }
}
