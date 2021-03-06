// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "ipchaingui.h"
#include "infowidget.h"
#include "ipchainunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "networkstyle.h"
#include "notificator.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "rpcconsole.h"
#include "utilitydialog.h"
#include "log/log.h"

#ifdef ENABLE_WALLET
#include "walletframe.h"
#include "walletmodel.h"
#endif // ENABLE_WALLET

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif

#include "chainparams.h"
#include "init.h"
#include "ui_interface.h"
#include "util.h"
#include "cmessagebox.h"
#include <iostream>

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopWidget>
#include <QDragEnterEvent>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
//#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QMessageBox>
#include "log/log.h"
#if QT_VERSION < 0x050000
#include <QTextDocument>
#include <QUrl>
#else
#include <QUrlQuery>
#endif
#define _PACKAGE_NAME "IPC"
const std::string BitcoinGUI::DEFAULT_UIPLATFORM =
        #if defined(Q_OS_MAC)
        "macosx"
        #elif defined(Q_OS_WIN)
        "windows"
        #else
        "other"
        #endif
        ;

/** Display name for default wallet name. Uses tilde to avoid name
 * collisions in the future with additional wallets */
const QString BitcoinGUI::DEFAULT_WALLET = "~Default";
extern int g_intMessageError;

Gerxy Gerxy_;
BitcoinGUI::BitcoinGUI(const PlatformStyle *_platformStyle, const NetworkStyle *networkStyle, QWidget *parent) :
    QMainWindow(parent),
    enableWallet(false),
    clientModel(0),
    walletFrame(0),
    quitAction(0),
    openRPCConsoleAction(0),
    trayIcon(0),
    trayIconMenu(0),
    notificator(0),
    rpcConsole(0),
    helpMessageDialog(0),
    prevBlocks(0),
    spinnerFrame(0),
    platformStyle(_platformStyle)
{

    GUIUtil::restoreWindowGeometry("nWindow", QSize(1000, 660), this);

    QString windowTitle = tr(_PACKAGE_NAME) + " - ";
#ifdef ENABLE_WALLET
    enableWallet = WalletModel::isWalletEnabled();
#endif // ENABLE_WALLET
    if(enableWallet)
    {
        windowTitle += tr("Wallet");
    } else {
        windowTitle += tr("Node");
    }
    windowTitle += " " + networkStyle->getTitleAddText();
#ifndef Q_OS_MAC
    QApplication::setWindowIcon(networkStyle->getTrayAndWindowIcon());
    setWindowIcon(networkStyle->getTrayAndWindowIcon());
#else
    MacDockIconHandler::instance()->setIcon(networkStyle->getAppIcon());
#endif
    setWindowTitle(windowTitle);

#ifdef Q_OS_WIN
    setWindowIcon(QIcon(":res/icons/bitcoin.ico"));
#else
    setWindowIcon(QIcon(":/icons/bitcoin"));
#endif


#if defined(Q_OS_MAC) && QT_VERSION < 0x050000
    // This property is not implemented in Qt 5. Setting it has no effect.
    // A replacement API (QtMacUnifiedToolBar) is available in QtMacExtras.
    setUnifiedTitleAndToolBarOnMac(true);
#endif
    this->setWindowFlags(this->windowFlags()&~Qt::WindowMaximizeButtonHint);
    rpcConsole = new RPCConsole(_platformStyle, 0);
#ifdef ENABLE_WALLET
    if(enableWallet)
    {
        /** Create wallet frame and make it the central widget */
        walletFrame = new WalletFrame(_platformStyle, this);

        setCentralWidget(walletFrame);
        setContentsMargins(0,0,0,0);
    } else
#endif // ENABLE_WALLET
    {
        /* When compiled without wallet or -disablewallet is provided,
         * the central widget is the rpc console.
         */
        setCentralWidget(rpcConsole);
    }


    setAcceptDrops(true);
    createActions();
    createTrayIcon(networkStyle);

    // Install event filter to be able to catch status tip events (QEvent::StatusTip)
    this->installEventFilter(this);

    // Subscribe to notifications from core
    subscribeToCoreSignals();



}

BitcoinGUI::~BitcoinGUI()
{
    // Unsubscribe from notifications from core
    unsubscribeFromCoreSignals();

    GUIUtil::saveWindowGeometry("nWindow", this);
    if(trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
        trayIcon->hide();
#ifdef Q_OS_MAC
    //   delete appMenuBar;
    MacDockIconHandler::cleanup();
#endif

    delete rpcConsole;
}

void BitcoinGUI::createActions()
{

#ifdef ENABLE_WALLET

#endif // ENABLE_WALLET

    openRPCConsoleAction = new QAction(platformStyle->TextColorIcon(":/icons/debugwindow"), tr("&Debug window"), this);
    openRPCConsoleAction->setStatusTip(tr("Open debugging and diagnostic console"));
    connect(openRPCConsoleAction, SIGNAL(triggered()), this, SLOT(showDebugWindow()));
    quitAction = new QAction(platformStyle->TextColorIcon(":/icons/quit"), tr("E&xit"), this);
    quitAction->setStatusTip(tr("Quit application"));
    quitAction->setMenuRole(QAction::QuitRole);
    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
    connect(quitAction, SIGNAL(triggered()), rpcConsole, SLOT(hide()));
#ifdef ENABLE_WALLET

#endif // ENABLE_WALLET

    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_C), this, SLOT(showDebugWindowActivateConsole()));
    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_D), this, SLOT(showDebugWindow()));
}


#include <QToolBar>


void BitcoinGUI::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;
    if(_clientModel)
    {
        // Create system tray menu (or setup the dock menu) that late to prevent users from calling actions,
        // while the client has not yet fully loaded
        createTrayIconMenu();//ly

        // Keep up to date with client
        updateNetworkState();
        connect(_clientModel, SIGNAL(numConnectionsChanged(int)), this, SLOT(setNumConnections(int)));
        connect(_clientModel, SIGNAL(networkActiveChanged(bool)), this, SLOT(setNetworkActive(bool)));

        setNumBlocks(_clientModel->getNumBlocks(), _clientModel->getLastBlockDate(), _clientModel->getVerificationProgress(NULL), false);
        connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(setNumBlocks(int,QDateTime,double,bool)));

        // Receive and report messages from client model
        connect(_clientModel, SIGNAL(message(QString,QString,unsigned int)), this, SLOT(message(QString,QString,unsigned int)));

        // Show progress dialog
        // connect(_clientModel, SIGNAL(showProgress(QString,int)), this, SLOT(showProgress(QString,int)));
        LogPrintf( "GUI:  BitcoinApplication::setClientModel rpcConsole->setClientModel\n");
        rpcConsole->setClientModel(_clientModel);
        LogPrintf( "GUI:  BitcoinApplication::setClientModel end rpcConsole->setClientModel\n");

#ifdef ENABLE_WALLET
        if(walletFrame)
        {
            walletFrame->setClientModel(_clientModel);
        }
#endif // ENABLE_WALLET
        //    unitDisplayControl->setOptionsModel(_clientModel->getOptionsModel());
        LogPrintf( "GUI:  BitcoinApplication::setClientModel OptionsModel* optionsModel\n");
        OptionsModel* optionsModel = _clientModel->getOptionsModel();
        if(optionsModel)
        {
            // be aware of the tray icon disable state change reported by the OptionsModel object.
            connect(optionsModel,SIGNAL(hideTrayIconChanged(bool)),this,SLOT(setTrayIconVisible(bool)));

            // initialize the disable state of the tray icon with the current value in the model.
            setTrayIconVisible(optionsModel->getHideTrayIcon());
        }
        LogPrintf( "GUI:  BitcoinApplication::setClientModel modalOverlay->setKnownBestHeight\n");

    } else {
        if(trayIconMenu)
        {
            // Disable context menu on tray icon
            trayIconMenu->clear();
        }
        // Propagate cleared model to child objects
        rpcConsole->setClientModel(nullptr);
#ifdef ENABLE_WALLET
        if (walletFrame)
        {
            walletFrame->setClientModel(nullptr);
        }
#endif // ENABLE_WALLET
        //  unitDisplayControl->setOptionsModel(nullptr);

    }
    LogPrintf( "GUI:  BitcoinApplication::setClientModel finish\n");
}

#ifdef ENABLE_WALLET
bool BitcoinGUI::addWallet(const QString& name, WalletModel *walletModel)
{

    if(!walletFrame)
        return false;

    this->walletModel_ =walletModel;
    return walletFrame->addWallet(name, walletModel);
}

bool BitcoinGUI::setCurrentWallet(const QString& name)
{
    if(!walletFrame)
        return false;
    return walletFrame->setCurrentWallet(name);
}

void BitcoinGUI::removeAllWallets()
{
    if(!walletFrame)
        return;
    walletFrame->removeAllWallets();
}
#endif // ENABLE_WALLET


void BitcoinGUI::createTrayIcon(const NetworkStyle *networkStyle)
{
#ifndef Q_OS_MAC
    trayIcon = new QSystemTrayIcon(this);
    QString toolTip = tr("%1 client").arg(tr(_PACKAGE_NAME)) + " " + networkStyle->getTitleAddText();
    trayIcon->setToolTip(toolTip);
    trayIcon->setIcon(networkStyle->getTrayAndWindowIcon());
    trayIcon->hide();
#endif

    notificator = new Notificator(QApplication::applicationName(), trayIcon, this);
    printf("BitcoinGUI::createTrayIcon \n \n");
}
void BitcoinGUI::setNumBlocks(int count, const QDateTime& blockDate, double nVerificationProgress, bool header)
{
    if (!clientModel)
        return;
    if(walletFrame)
    {

        InfoWidget * singleton1 = InfoWidget::GetInstance();
        singleton1->fresh(count, blockDate,  nVerificationProgress,header);

        int numcon = clientModel->getNumConnections(((1U << 0) | (1U << 1)));
        walletFrame->headshowchaininfo(numcon,count,  header);
    }
    enum BlockSource blockSource = clientModel->getBlockSource();
    switch (blockSource) {
    case BLOCK_SOURCE_NETWORK:
        if (header) {
            updateHeadersSyncProgressLabel();
            return;
        }
        updateHeadersSyncProgressLabel();
        break;
    case BLOCK_SOURCE_DISK:
        if (header) {
        } else {
        }
        break;
    case BLOCK_SOURCE_REINDEX:
        break;
    case BLOCK_SOURCE_NONE:
        if (header) {
            return;
        }
        break;
    }
    QString tooltip;

    QDateTime currentDate = QDateTime::currentDateTime();
    qint64 secs = blockDate.secsTo(currentDate);

    tooltip = tr("Processed %n block(s) of transaction history.", "", count);
    if(secs < 90*60)
    {
        tooltip = tr("Up to date") + QString(".<br>") + tooltip;

    }
    else
    {
        QString timeBehindText = GUIUtil::formatNiceTimeOffset(secs);
        tooltip = tr("Catching up...") + QString("<br>") + tooltip;
        if(count != prevBlocks)
        {
        }
        prevBlocks = count;

#ifdef ENABLE_WALLET

#endif // ENABLE_WALLET

        tooltip += QString("<br>");
        tooltip += tr("Last received block was generated %1 ago.").arg(timeBehindText);
        tooltip += QString("<br>");
        tooltip += tr("Transactions after this will not yet be visible.");
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

}

void BitcoinGUI::createTrayIconMenu()
{
#ifndef Q_OS_MAC
    if (!trayIcon)
        return;

    trayIconMenu = new QMenu(this);
    trayIcon->setContextMenu(trayIconMenu);

    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
#else
    // Note: On Mac, the dock icon is used to provide the tray's functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    dockIconHandler->setMainWindow((QMainWindow *)this);
    trayIconMenu = dockIconHandler->dockMenu();
#endif
    if(fPrintToDebugLog)
        trayIconMenu->addAction(openRPCConsoleAction);//ly fortest

#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif
}

#ifndef Q_OS_MAC
void BitcoinGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
    {
        // Click on system tray icon triggers show/hide of the main window
        toggleHidden();
    }
}
#endif

void BitcoinGUI::showDebugWindow()
{
    rpcConsole->showNormal();
    rpcConsole->show();
    rpcConsole->raise();
    rpcConsole->activateWindow();
}

void BitcoinGUI::showDebugWindowActivateConsole()
{
    rpcConsole->setTabFocus(RPCConsole::TAB_CONSOLE);
    showDebugWindow();
}


#ifdef ENABLE_WALLET
void BitcoinGUI::gotoOverviewPage()
{
    if (walletFrame) walletFrame->gotoOverviewPage();
}

void BitcoinGUI::gotoIPCPage()
{
    if (walletFrame) walletFrame->gotoIpcPage();
}

void BitcoinGUI::gotoSetPage()
{
    if (walletFrame) walletFrame->gotoSetPage();
}

void BitcoinGUI::gotoReceiveCoinsPage()
{
    if (walletFrame) walletFrame->gotoReceiveCoinsPage();
}

void BitcoinGUI::gotoSendCoinsPage(QString addr)
{
    if (walletFrame) walletFrame->gotoSendCoinsPage(addr);
}

#endif // ENABLE_WALLET

void BitcoinGUI::updateNetworkState()
{
    int count = clientModel->getNumConnections();
    QString icon;
    switch(count)
    {
    case 0: icon = ":/icons/connect_0"; break;
    case 1: case 2: case 3: icon = ":/icons/connect_1"; break;
    case 4: case 5: case 6: icon = ":/icons/connect_2"; break;
    case 7: case 8: case 9: icon = ":/icons/connect_3"; break;
    default: icon = ":/icons/connect_4"; break;
    }

    QString tooltip;

    if (clientModel->getNetworkActive()) {
        tooltip = tr("%n active connection(s) to Bitcoin network", "", count) + QString(".<br>") + tr("Click to disable network activity.");
    } else {
        tooltip = tr("Network activity disabled.") + QString("<br>") + tr("Click to enable network activity again.");
        icon = ":/icons/network_disabled";
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");
}

void BitcoinGUI::setNumConnections(int count)
{
    updateNetworkState();
}

void BitcoinGUI::setNetworkActive(bool networkActive)
{
    updateNetworkState();
}

void BitcoinGUI::updateHeadersSyncProgressLabel()
{
    int64_t headersTipTime = clientModel->getHeaderTipTime();
    int headersTipHeight = clientModel->getHeaderTipHeight();
    int estHeadersLeft = (GetTime() - headersTipTime) / Params().GetConsensus().nPowTargetSpacing;
}

void BitcoinGUI::message(const QString &title, const QString &message, unsigned int style, bool *ret)
{
    QString trmessage;
    if(message == "wallet.dat corrupt, salvage failed")
    {
        g_intMessageError = 2;
        trmessage = tr("wallet.dat corrupt, salvage failed");
    }

    QString strTitle = tr("IPC"); // default title
    // Default to information icon
    int nMBoxIcon = QMessageBox::Information;
    int nNotifyIcon = Notificator::Information;

    QString msgType;

    // Prefer supplied title over style based title
    if (!title.isEmpty()) {
        msgType = title;
    }
    else {
        switch (style) {
        case CClientUIInterface::MSG_ERROR:
            msgType = tr("Error");
            break;
        case CClientUIInterface::MSG_WARNING:
            msgType = tr("Warning");
            break;
        case CClientUIInterface::MSG_INFORMATION:
            msgType = tr("Information");
            break;
        default:
            break;
        }
    }
    // Append title to "Bitcoin - "
    if (!msgType.isEmpty())
        strTitle += " - " + msgType;

    // Check for error/warning icon
    if (style & CClientUIInterface::ICON_ERROR) {
        nMBoxIcon = QMessageBox::Critical;
        nNotifyIcon = Notificator::Critical;
    }
    else if (style & CClientUIInterface::ICON_WARNING) {
        nMBoxIcon = QMessageBox::Warning;
        nNotifyIcon = Notificator::Warning;
    }

    // Display message
    if (style & CClientUIInterface::MODAL) {
        // Check for buttons, use OK as default, if none was supplied
        QMessageBox::StandardButton buttons;
        if (!(buttons = (QMessageBox::StandardButton)(style & CClientUIInterface::BTN_MASK)))
            buttons = QMessageBox::Ok;

        showNormalIfMinimized();
        if(g_intMessageError >1){
            QMessageBox mBox((QMessageBox::Icon)nMBoxIcon, strTitle, trmessage, buttons, this);
            int r = mBox.exec();
            if (ret != NULL)
                *ret = r == QMessageBox::Ok;
        }else{
            QMessageBox mBox((QMessageBox::Icon)nMBoxIcon, strTitle, message, buttons, this);
            int r = mBox.exec();
            if (ret != NULL)
                *ret = r == QMessageBox::Ok;
        }
    }
    else
        notificator->notify((Notificator::Class)nNotifyIcon, strTitle, message);
}

void BitcoinGUI::changeEvent(QEvent *e)
{

    QMainWindow::changeEvent(e);
#ifndef Q_OS_MAC // Ignored on Mac
    if(e->type() == QEvent::WindowStateChange)
    {
        if(clientModel && clientModel->getOptionsModel() && clientModel->getOptionsModel()->getMinimizeToTray())
        {
            QWindowStateChangeEvent *wsevt = static_cast<QWindowStateChangeEvent*>(e);
            if(!(wsevt->oldState() & Qt::WindowMinimized) && isMinimized())
            {
                QTimer::singleShot(0, this, SLOT(hide()));
                e->ignore();
            }
        }
    }
#endif

    Gerxy_.x_=this->x();
    Gerxy_.y_=this->y();
}

void BitcoinGUI::closeEvent(QCloseEvent *event)
{
#ifndef Q_OS_MAC // Ignored on Mac
    if(clientModel && clientModel->getOptionsModel())
    {
        CMessageBox msg;

        msg.setWalletModel(this->walletModel_);

        msg.setGeometry(this->x()+(this->width()-msg.width())/2,
                        this->y()+(this->height()-msg.height())/2,msg.width(),msg.height());
        msg.setIsClose(true);
        msg.exec();

        if(msg.m_answertype == 0)
        {
            event->ignore();
            return;
        }

        if(!clientModel->getOptionsModel()->getMinimizeOnClose())
        {

            // close rpcConsole in case it was open to make some space for the shutdown window
            rpcConsole->close();

            QApplication::quit();
        }
        else
        {
            QMainWindow::showMinimized();
            event->ignore();
        }
    }
#else
    QMainWindow::closeEvent(event);
#endif
}

#ifdef ENABLE_WALLET

#endif // ENABLE_WALLET

void BitcoinGUI::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept only URIs
    if(event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void BitcoinGUI::dropEvent(QDropEvent *event)
{
    if(event->mimeData()->hasUrls())
    {
        Q_FOREACH(const QUrl &uri, event->mimeData()->urls())
        {
            Q_EMIT receivedURI(uri.toString());
        }
    }
    event->acceptProposedAction();
}

bool BitcoinGUI::eventFilter(QObject *object, QEvent *event)
{
    return QMainWindow::eventFilter(object, event);
}

#ifdef ENABLE_WALLET
bool BitcoinGUI::handlePaymentRequest(const SendCoinsRecipient& recipient)
{
    // URI has to be valid
    if (walletFrame )
    {
        showNormalIfMinimized();
        gotoSendCoinsPage();
        return true;
    }
    return false;
}

void BitcoinGUI::setHDStatus(int hdEnabled)
{
}

void BitcoinGUI::setEncryptionStatus(int status)
{
    switch(status)
    {
    case WalletModel::Unencrypted:

        break;
    case WalletModel::Unlocked:

        break;
    case WalletModel::Locked:

        break;
    }
}
#endif // ENABLE_WALLET

void BitcoinGUI::showNormalIfMinimized(bool fToggleHidden)
{
    if(!clientModel)
        return;
    if (isHidden())
    {
        show();
        activateWindow();
    }
    else if (isMinimized())
    {
        showNormal();
        activateWindow();
    }
    else if (GUIUtil::isObscured(this))
    {
        raise();
        activateWindow();
    }
    else if(fToggleHidden)
        hide();
}

void BitcoinGUI::toggleHidden()
{
    showNormalIfMinimized(true);
}

void BitcoinGUI::detectShutdown()
{
    if (ShutdownRequested())
    {
        if(rpcConsole)
            rpcConsole->hide();
        qApp->quit();
    }
}

void BitcoinGUI::setTrayIconVisible(bool fHideTrayIcon)
{
    if (trayIcon)
    {
        trayIcon->setVisible(!fHideTrayIcon);
    }
}


static bool ThreadSafeMessageBox(BitcoinGUI *gui, const std::string& message, const std::string& caption, unsigned int style)
{
    bool modal = (style & CClientUIInterface::MODAL);
    style &= ~CClientUIInterface::SECURE;
    bool ret = false;
    // In case of modal message, use blocking connection to wait for user to click a button

    QMetaObject::invokeMethod(gui, "message",
                              modal ? GUIUtil::blockingGUIThreadConnection() : Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(caption)),
                              Q_ARG(QString, QString::fromStdString(message)),
                              Q_ARG(unsigned int, style),
                              Q_ARG(bool*, &ret));
    return ret;
}

void BitcoinGUI::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.ThreadSafeMessageBox.connect(boost::bind(ThreadSafeMessageBox, this, _1, _2, _3));
    uiInterface.ThreadSafeQuestion.connect(boost::bind(ThreadSafeMessageBox, this, _1, _3, _4));
}

void BitcoinGUI::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.ThreadSafeMessageBox.disconnect(boost::bind(ThreadSafeMessageBox, this, _1, _2, _3));
    uiInterface.ThreadSafeQuestion.disconnect(boost::bind(ThreadSafeMessageBox, this, _1, _3, _4));
}

void BitcoinGUI::toggleNetworkActive()
{
    if (clientModel) {
        clientModel->setNetworkActive(!clientModel->getNetworkActive());
    }
}

QString BitcoinGUI::getCurrentTime(QString info)
{
    QDateTime current_date_time = QDateTime::currentDateTime();
    QString current_date = current_date_time.toString("yyyy-MM-dd");
    QString current_time = current_date_time.toString("hh:mm:ss.zzz ");
    return current_time;
}
