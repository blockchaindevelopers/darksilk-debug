/*
 * Qt5 DarkSilk GUI.
 *
 * W.J. van der Laan 2011-2016
 * The Bitcoin Developers 2011-2016
 * Silk Network 2015-2016
 */

#include <QApplication>
#include <QMenuBar>
#include <QMenu>
#include <QFile>
#include <QIcon>
#include <QVBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QProgressDialog>
#include <QStackedWidget>
#include <QDateTime>
#include <QMovie>
#include <QFileDialog>
#include <QDesktopServices>
#include <QTimer>
#include <QDragEnterEvent>
#include <QUrl>
#include <QMimeData>
#include <QStyle>
#include <QToolButton>
#include <QScrollArea>
#include <QScroller>
#include <QTextDocument>
#include <iostream>

#include "darksilkgui.h"
#include "transactiontablemodel.h"
#include "addressbookpage.h"
#include "sendcoinsdialog.h"
#include "signverifymessagedialog.h"
#include "optionsdialog.h"
#include "aboutdialog.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "messagemodel.h"
#include "editaddressdialog.h"
#include "optionsmodel.h"
#include "transactiondescdialog.h"
#include "addresstablemodel.h"
#include "transactionview.h"
#include "overviewpage.h"
#include "darksilkunits.h"
#include "guiconstants.h"
#include "askpassphrasedialog.h"
#include "notificator.h"
#include "guiutil.h"
#include "debugconsole.h"
#include "wallet/wallet.h"
#include "init.h"
#include "anon/stormnode/stormnode-sync.h"
#include "stormnodemanager.h"
// #include "blockbrowser.h"
// #include "statisticspage.h"
#include "messagepage.h"

#ifdef USE_NATIVE_I2P
#include "showi2paddresses.h"
#endif

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif

extern bool fOnlyTor;
extern CWallet* pwalletMain;
extern int64_t nLastCoinStakeSearchInterval;
double GetPoSKernelPS();

DarkSilkGUI::DarkSilkGUI(QWidget *parent):
    QMainWindow(parent),
    clientModel(0),
    walletModel(0),
    toolbar(0),
    encryptWalletAction(0),
    changePassphraseAction(0),
    unlockWalletAction(0),
    lockWalletAction(0),
    aboutQtAction(0),
    trayIcon(0),
    notificator(0),
    debugConsole(0),
    prevBlocks(0),
    nWeight(0)
{
    resize(850+95, 550);
    setWindowTitle(tr("DarkSilk") + " - " + tr("Wallet"));
#ifndef Q_OS_MAC
    qApp->setWindowIcon(QIcon(":icons/darksilk"));
    setWindowIcon(QIcon(":icons/darksilk"));
#else
    //setUnifiedTitleAndToolBarOnMac(true);
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus);
#endif
    // Accept D&D of URIs
    setAcceptDrops(true);

    // Create actions for the toolbar, menu bar and tray/dock icon
    createActions();

    // Create application menu bar
    createMenuBar();

    // Create the toolbars
    createToolBars();

    // Create the tray icon (or setup the dock icon)
    createTrayIcon();

    QPalette p;
    p.setColor(QPalette::Window, QColor(0, 0, 0));
    p.setColor(QPalette::Text, QColor(67, 67, 67));
    setPalette(p);

    // Create tabs
    overviewPage = new OverviewPage();

    receiveCoinsPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::ReceivingTab);

    sendCoinsPage = new SendCoinsDialog(this);

    transactionsPage = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout();
    transactionView = new TransactionView(this);
    vbox->addWidget(transactionView);
    transactionsPage->setLayout(vbox);

    addressBookPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::SendingTab);    

    signVerifyMessageDialog = new SignVerifyMessageDialog(this);

    /*blockBrowser = new BlockBrowser(this);

    statisticsPage = new StatisticsPage(this);
*/
    messagePage = new MessagePage(this);

    stormnodeManagerPage = 0;


    centralStackedWidget = new QStackedWidget(this);
    centralStackedWidget->setContentsMargins(0, 0, 0, 0);
    centralStackedWidget->addWidget(overviewPage);
    centralStackedWidget->addWidget(messagePage);
    /*centralStackedWidget->addWidget(blockBrowser);
    centralStackedWidget->addWidget(statisticsPage);
    */centralStackedWidget->addWidget(transactionsPage);
    centralStackedWidget->addWidget(addressBookPage);
    centralStackedWidget->addWidget(receiveCoinsPage);
    centralStackedWidget->addWidget(sendCoinsPage);

    QWidget *centralWidget = new QWidget();
    QVBoxLayout *centralLayout = new QVBoxLayout(centralWidget);

    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralWidget->setContentsMargins(0, 0, 0, 0);
#ifndef Q_OS_MAC
    centralLayout->addWidget(appMenuBar);
#endif
    centralLayout->addWidget(centralStackedWidget);

    setCentralWidget(centralWidget);

    // Create status bar
    statusBar();

    // Disable size grip because it looks ugly and nobody needs it
    statusBar()->setSizeGripEnabled(false);

    // Status bar notification icons
    QWidget *frameBlocks = new QWidget();
    frameBlocks->setContentsMargins(0, 0, 0, 0);
    frameBlocks->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    frameBlocks->setStyleSheet("QWidget { background: none; margin-bottom: 5px; }");

    QHBoxLayout *frameBlocksLayout = new QHBoxLayout(frameBlocks);
    frameBlocksLayout->setContentsMargins(3, 0, 3, 0);
    frameBlocksLayout->setSpacing(3);
    frameBlocksLayout->setAlignment(Qt::AlignHCenter);

    labelEncryptionIcon = new GUIUtil::ClickableLabel();
    labelStakingIcon = new QLabel();
    labelConnectionsIcon = new QLabel();
    labelBlocksIcon = new QLabel();

#ifdef USE_NATIVE_I2P
    labelI2PConnections = new QLabel();
    labelI2POnly = new QLabel();
    labelI2PGenerated = new QLabel();

    frameBlocksLayout->addWidget(labelI2PGenerated);
    frameBlocksLayout->addWidget(labelI2POnly);
    frameBlocksLayout->addWidget(labelI2PConnections);
#endif

    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelEncryptionIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelStakingIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelConnectionsIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelBlocksIcon);
    frameBlocksLayout->addStretch();
    

    if (GetBoolArg("-staking", true)) {
        QTimer *timerStakingIcon = new QTimer(labelStakingIcon);

        connect(timerStakingIcon, SIGNAL(timeout()), this, SLOT(updateStakingIcon()));
        timerStakingIcon->start(30 * 1000);
        updateStakingIcon();
    }

    // Progress bar and label for blocks download
    progressBarLabel = new QLabel();
    progressBarLabel->setVisible(false);

    progressBar = new QProgressBar();
    progressBar->setAlignment(Qt::AlignCenter);
    progressBar->setVisible(false);

    if (!fUseBlackTheme) {
        // Override style sheet for progress bar for styles that have a segmented progress bar,
        // as they make the text unreadable (workaround for issue #1071)
        // See https://qt-project.org/doc/qt-4.8/gallery.html
        QString curStyle = qApp->style()->metaObject()->className();

        if(curStyle == "QWindowsStyle" || curStyle == "QWindowsXPStyle") {
            progressBar->setStyleSheet("QProgressBar { background-color: rgb(0,0,0); border: 1px solid grey; border-radius: 7px; padding: 1px; text-align: center; } QProgressBar::chunk { background: QLinearGradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #220050, stop: 1 #500078); border-radius: 7px; margin: 0px; }");

            appMenuBar->setStyleSheet("QMenuBar { background-color: rgb(0,0,0); }");
        }
    }

    statusBar()->addWidget(progressBarLabel);
    statusBar()->addWidget(progressBar);
    statusBar()->addPermanentWidget(frameBlocks);
    statusBar()->setObjectName("statusBar");
    statusBar()->setStyleSheet("#statusBar { background-color: qradialgradient(cx: -0.8, cy: 0, fx: -0.8, fy: 0, radius: 0.6, stop: 0 #000000, stop: 1 #000000);  }");

    syncIconMovie = new QMovie(fUseBlackTheme ? ":/movies/update_spinner_black" : ":/movies/update_spinner", "mng", this);

    // Clicking on a transaction on the overview page simply sends you to transaction history page
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), this, SLOT(gotoHistoryPage()));
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), transactionView, SLOT(focusTransaction(QModelIndex)));

    // Double-clicking on a transaction on the transaction history page shows details
    connect(transactionView, SIGNAL(doubleClicked(QModelIndex)), transactionView, SLOT(showDetails()));

    // Clicking on "Verify Message" in the address book sends you to the verify message tab
    connect(addressBookPage, SIGNAL(verifyMessage(QString)), this, SLOT(gotoVerifyMessageTab(QString)));

    // Clicking on "Sign Message" in the receive coins page sends you to the sign message tab
    connect(receiveCoinsPage, SIGNAL(signMessage(QString)), this, SLOT(gotoSignMessageTab(QString)));

    debugConsole = new DEBUGConsole(this);
    connect(openDEBUGConsoleAction, SIGNAL(triggered()), debugConsole, SLOT(show()));
    connect(openPeersAction, SIGNAL(triggered()), debugConsole, SLOT(showPeers()));
    // prevents an open debug window from becoming stuck/unusable on client shutdown
    connect(quitAction, SIGNAL(triggered()), debugConsole, SLOT(hide())); // prevents an open debug window from becoming stuck/unusable on client shutdown

    gotoOverviewPage();
}

DarkSilkGUI::~DarkSilkGUI() 
{
    if(trayIcon) { // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
        trayIcon->hide();
    }
#ifdef Q_OS_MAC
    delete appMenuBar;
    MacDockIconHandler::instance()->setMainWindow(NULL);
#endif
}

void DarkSilkGUI::createActions() 
{
    QActionGroup *tabGroup = new QActionGroup(this);

    overviewAction = new QAction(QIcon(":/icons/overview"), tr("&Dashboard"), this);
    overviewAction->setToolTip(tr("Show general overview of wallet"));
    overviewAction->setCheckable(true);
#ifdef Q_OS_MAC
    overviewAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_1));
#else
    overviewAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_1));
#endif
    tabGroup->addAction(overviewAction);

    receiveCoinsAction = new QAction(QIcon(":/icons/receiving_addresses"), tr("&Receive"), this);
    receiveCoinsAction->setToolTip(tr("Show the list of addresses for receiving payments"));
    receiveCoinsAction->setCheckable(true);
#ifdef Q_OS_MAC
    receiveCoinsAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_3));
#else
    receiveCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_2));
#endif
    tabGroup->addAction(receiveCoinsAction);

    sendCoinsAction = new QAction(QIcon(":/icons/send"), tr("&Send"), this);
    sendCoinsAction->setToolTip(tr("Send coins to a DarkSilk address"));
    sendCoinsAction->setCheckable(true);
#ifdef Q_OS_MAC
    sendCoinsAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_2));
#else
    sendCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_3));
#endif
    tabGroup->addAction(sendCoinsAction);

    historyAction = new QAction(QIcon(":/icons/history"), tr("&Transactions"), this);
    historyAction->setToolTip(tr("Browse transaction history"));
    historyAction->setCheckable(true);
#ifdef Q_OS_MAC
    historyAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_4));
#else
    historyAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_4));
#endif
    tabGroup->addAction(historyAction);

    addressBookAction = new QAction(QIcon(":/icons/address-book"), tr("&Address Book"), this);
    addressBookAction->setToolTip(tr("Edit the list of stored addresses and labels"));
    addressBookAction->setCheckable(true);
#ifdef Q_OS_MAC
    addressBookAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_5));
#else
    addressBookAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_5));
#endif
    tabGroup->addAction(addressBookAction);
/*
    statisticsAction = new QAction(QIcon(":/icons/statistics"), tr("&Statistics"), this);
    statisticsAction->setToolTip(tr("DRKSLK PoW/PoS Statistics"));
    statisticsAction->setCheckable(true);
#ifdef Q_OS_MAC
    statisticsAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_6));
#else
    statisticsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_6));
#endif
    tabGroup->addAction(statisticsAction);

    blockAction = new QAction(QIcon(":/icons/block"), tr("&Block Explorer"), this);
    blockAction->setToolTip(tr("Explore the BlockChain"));
    blockAction->setCheckable(true);
#ifdef Q_OS_MAC
    blockAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_7));
#else
    blockAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_7));
#endif
    tabGroup->addAction(blockAction);
*/
    messageAction = new QAction(QIcon(":/icons/edit"), tr("Encrypted Messages"), this);
    messageAction->setToolTip(tr("View and Send Encrypted messages"));
    messageAction->setCheckable(true);
#ifdef Q_OS_MAC
    messageAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_8));
#else
    messageAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_8));
#endif
    tabGroup->addAction(messageAction);

    stormnodeManagerAction = new QAction(QIcon(":/icons/darksilk"), tr("&Stormnode Network"), this);
    stormnodeManagerAction->setToolTip(tr("Show Stormnodes status and configure your nodes."));
    stormnodeManagerAction->setCheckable(true);
#ifdef Q_OS_MAC
    stormnodeManagerAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_9));
#else
    stormnodeManagerAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_9));
#endif
    tabGroup->addAction(stormnodeManagerAction);

    connect(overviewAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(gotoOverviewPage()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(gotoHistoryPage()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(gotoAddressBookPage()));
    /*connect(blockAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(blockAction, SIGNAL(triggered()), this, SLOT(gotoBlockBrowser()));
    connect(statisticsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(statisticsAction, SIGNAL(triggered()), this, SLOT(gotoStatisticsPage()));
    */connect(messageAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(messageAction, SIGNAL(triggered()), this, SLOT(gotoMessagePage()));

    connect(stormnodeManagerAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(stormnodeManagerAction, SIGNAL(triggered()), this, SLOT(gotoStormnodeManagerPage()));

    quitAction = new QAction(tr("E&xit"), this);
    quitAction->setToolTip(tr("Quit application"));
    quitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);

    aboutAction = new QAction(tr("&About DarkSilk"), this);
    aboutAction->setToolTip(tr("Show information about DarkSilk"));
    aboutAction->setMenuRole(QAction::AboutRole);

    aboutQtAction = new QAction(tr("About &Qt"), this);
    aboutQtAction->setToolTip(tr("Show information about Qt"));
    aboutQtAction->setMenuRole(QAction::AboutQtRole);

    optionsAction = new QAction(tr("&Options..."), this);
    optionsAction->setToolTip(tr("Modify configuration options for DarkSilk"));
    optionsAction->setMenuRole(QAction::PreferencesRole);

    toggleHideAction = new QAction(QIcon(":/icons/darksilk"), tr("&Show / Hide"), this);

    encryptWalletAction = new QAction(tr("&Encrypt Wallet..."), this);
    encryptWalletAction->setToolTip(tr("Encrypt or decrypt wallet"));

    backupWalletAction = new QAction(tr("&Backup Wallet..."), this);
    backupWalletAction->setToolTip(tr("Backup wallet to another location"));

    changePassphraseAction = new QAction(tr("&Change Passphrase..."), this);
    changePassphraseAction->setToolTip(tr("Change the passphrase used for wallet encryption"));

    unlockWalletAction = new QAction(tr("&Unlock Wallet..."), this);
    unlockWalletAction->setToolTip(tr("Unlock wallet"));

    lockWalletAction = new QAction(tr("&Lock Wallet"), this);
    lockWalletAction->setToolTip(tr("Lock wallet"));

    signMessageAction = new QAction(tr("Sign &message..."), this);

    verifyMessageAction = new QAction(tr("&Verify message..."), this);

    exportAction = new QAction(tr("&Export..."), this);
    exportAction->setToolTip(tr("Export the data in the current tab to a file"));

    openDEBUGConsoleAction = new QAction(tr("&Debug window"), this);
    openDEBUGConsoleAction->setToolTip(tr("Open debugging and diagnostic console"));

    openPeersAction = new QAction(QIcon(":/icons/connect_4"), tr("&Peers list"), this);
    openPeersAction->setStatusTip(tr("Show peers info"));

    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(aboutClicked()));
    connect(aboutQtAction, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(optionsAction, SIGNAL(triggered()), this, SLOT(optionsClicked()));
    connect(toggleHideAction, SIGNAL(triggered()), this, SLOT(toggleHidden()));
    connect(encryptWalletAction, SIGNAL(triggered()), this, SLOT(encryptWallet()));
    connect(backupWalletAction, SIGNAL(triggered()), this, SLOT(backupWallet()));
    connect(changePassphraseAction, SIGNAL(triggered()), this, SLOT(changePassphrase()));
    connect(unlockWalletAction, SIGNAL(triggered()), this, SLOT(unlockWallet()));
    connect(lockWalletAction, SIGNAL(triggered()), this, SLOT(lockWallet()));
    connect(signMessageAction, SIGNAL(triggered()), this, SLOT(gotoSignMessageTab()));
    connect(verifyMessageAction, SIGNAL(triggered()), this, SLOT(gotoVerifyMessageTab()));
}

void DarkSilkGUI::createMenuBar() 
{
#ifdef Q_OS_MAC
    appMenuBar = new QMenuBar();
#else
    appMenuBar = menuBar();
#endif

    // workaround for unity's global menu
    if (qgetenv("QT_QPA_PLATFORMTHEME") == "appmenu-qt5")
        appMenuBar = menuBar();
    else
        appMenuBar = new QMenuBar();

    // Configure the menus
    QMenu *file = appMenuBar->addMenu(tr("&File"));
    file->addAction(backupWalletAction);
    file->addAction(exportAction);
    file->addAction(signMessageAction);
    file->addAction(verifyMessageAction);
    file->addSeparator();
    file->addAction(quitAction);

    QMenu *settings = appMenuBar->addMenu(tr("&Settings"));
    settings->addAction(encryptWalletAction);
    settings->addAction(changePassphraseAction);
    settings->addAction(unlockWalletAction);
    settings->addAction(lockWalletAction);
    settings->addSeparator();
    settings->addAction(optionsAction);

    QMenu *help = appMenuBar->addMenu(tr("&Help"));
    help->addAction(openDEBUGConsoleAction);
    help->addAction(openPeersAction);
    help->addSeparator();
    help->addAction(aboutAction);
    help->addAction(aboutQtAction);
}

static QWidget* makeToolBarSpacer() 
{
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    spacer->setStyleSheet(fUseBlackTheme ? "QWidget { background: rgb(0,0,0); }" : "QWidget { background: none; }");
    return spacer;
}

/*static QWidget* makeHeaderSpacer()

 {
    QWidget* spacer = new QWidget();
    spacer->setMinimumSize(0,10);
    spacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    spacer->setStyleSheet(fUseBlackTheme ? "QWidget { background: rgb(0,0,0); }" : "QWidget { background: none; }");
    return spacer;
}*/

void DarkSilkGUI::createToolBars() 
{
    QLabel* header = new QLabel();
    header->setMinimumSize(0, 48);
    header->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    header->setPixmap(QPixmap(":/icons/header"));

    toolbar = new QToolBar(tr("Tabs toolbar"));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    toolbar->setObjectName("tabs");
    toolbar->setStyleSheet("#tabs { background-color: qradialgradient(cx: -0.8, cy: 0, fx: -0.8, fy: 0, radius: 0.6, stop: 0 #000000, stop: 1 #000000);  }");
    toolbar->addWidget(header);

    QMenu *toolbarMenu = new QMenu();
    toolbarMenu->addAction(overviewAction);
    toolbarMenu->addAction(receiveCoinsAction);
    toolbarMenu->addAction(sendCoinsAction);
    toolbarMenu->addAction(historyAction);
    toolbarMenu->addAction(addressBookAction);
    /*toolbarMenu->addAction(statisticsAction);
    toolbarMenu->addAction(blockAction);
    */toolbarMenu->addAction(messageAction);
    toolbarMenu->addAction(stormnodeManagerAction);

    QAction* menuAction = new QAction(QIcon(":/icons/overview"), tr("&Menu"), this);
    menuAction->setToolTip(tr("Access DarkSilk Wallet Tabs"));
    menuAction->setCheckable(false);

    QToolButton* menuToolButton = new QToolButton();
    menuToolButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    menuToolButton->setMenu(toolbarMenu);
    menuToolButton->setPopupMode(QToolButton::InstantPopup);
    menuToolButton->setDefaultAction(menuAction);

    netLabel = new QLabel();
    netLabel->setObjectName("netLabel");
    netLabel->setStyleSheet("#netLabel { color: #ffffff; }");

    toolbar->addWidget(menuToolButton);
    toolbar->addWidget(makeToolBarSpacer());
    toolbar->addWidget(netLabel);
    toolbar->setOrientation(Qt::Horizontal);
    toolbar->setMovable(false);

    addToolBar(Qt::TopToolBarArea, toolbar);
}

void DarkSilkGUI::setClientModel(ClientModel *clientModel) 
{
    if(!fOnlyTor) 
    {
#ifdef USE_NATIVE_I2P
        setNumI2PConnections(clientModel->getNumI2PConnections());
        connect(clientModel, SIGNAL(numI2PConnectionsChanged(int)), this, SLOT(setNumI2PConnections(int)));
    if(clientModel->isI2POnly()) {
            netLabel->setText("I2P");
            netLabel->setToolTip(tr("Wallet is using I2P-network only"));
        } else {
            netLabel->setText("CLEARNET");
        }
#endif

#ifdef USE_NATIVE_I2P
        if (clientModel->isI2PAddressGenerated()) 
        {
            labelI2PGenerated->setText("DYN");
            labelI2PGenerated->setToolTip(tr("Wallet is running with a random generated I2P-address"));
        } else {
            labelI2PGenerated->setText("STA");
            labelI2PGenerated->setToolTip(tr("Wallet is running with a static I2P-address"));
        }
#endif  
        } else { 
        if(!IsLimited(NET_TOR)) 
        {
            netLabel->setMinimumSize(48, 48);
            netLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            netLabel->setPixmap(QPixmap(":/icons/onion"));
            netLabel->setMaximumSize(48, 48);
            netLabel->setScaledContents(true);
        } else {
            netLabel->setText("CLEARNET");
        }
    }

    this->clientModel = clientModel;

    if(clientModel) 
    {
        // Replace some strings and icons, when using the testnet
        if(clientModel->isTestNet()) 
        {
            setWindowTitle(windowTitle() + QString(" ") + tr("[testnet]"));
#ifndef Q_OS_MAC
            qApp->setWindowIcon(QIcon(":icons/darksilk_testnet"));
            setWindowIcon(QIcon(":icons/darksilk_testnet"));
#else
            MacDockIconHandler::instance()->setIcon(QIcon(":icons/darksilk_testnet"));
#endif

            if(trayIcon) 
            {
                trayIcon->setToolTip(tr("DarkSilk client") + QString(" ") + tr("[testnet]"));
                trayIcon->setIcon(QIcon(":/icons/toolbar_testnet"));
                toggleHideAction->setIcon(QIcon(":/icons/toolbar_testnet"));
            }
        }

        // Keep up to date with client
        setNumConnections(clientModel->getNumConnections());
        connect(clientModel, SIGNAL(numConnectionsChanged(int)), this, SLOT(setNumConnections(int)));

        // Show progress dialog
        connect(clientModel, SIGNAL(showProgress(QString,int)), this, SLOT(showProgress(QString,int)));

        setNumBlocks(clientModel->getNumBlocks());
        connect(clientModel, SIGNAL(numBlocksChanged(int)), this, SLOT(setNumBlocks(int)));

        // Receive and report messages from network/worker thread
        connect(clientModel, SIGNAL(message(QString, QString, bool, unsigned int)), this, SLOT(message(QString, QString, bool, unsigned int)));

        overviewPage->setClientModel(clientModel);
        debugConsole->setClientModel(clientModel);
    }
}

void DarkSilkGUI::setWalletModel(WalletModel *walletModel) 
{
    this->walletModel = walletModel;
    if(walletModel) 
    {
        // Receive and report messages from wallet thread
        connect(walletModel, SIGNAL(message(QString, QString, bool, unsigned int)), this, SLOT(message(QString, QString, bool, unsigned int)));

        // Put transaction list in tabs
        transactionView->setModel(walletModel);
        overviewPage->setWalletModel(walletModel);
        addressBookPage->setModel(walletModel->getAddressTableModel());
        receiveCoinsPage->setModel(walletModel->getAddressTableModel());
        sendCoinsPage->setModel(walletModel);
        signVerifyMessageDialog->setModel(walletModel);

        setEncryptionStatus(walletModel->getEncryptionStatus());
        connect(walletModel, SIGNAL(encryptionStatusChanged(int)), this, SLOT(setEncryptionStatus(int)));

        // Balloon pop-up for new transaction
        connect(walletModel->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex, int, int)),
                this, SLOT(incomingTransaction(QModelIndex, int, int)));

        // Ask for passphrase if needed
        connect(walletModel, SIGNAL(requireUnlock()), this, SLOT(unlockWallet()));
    }
}

void DarkSilkGUI::setMessageModel(MessageModel *messageModel)
{
    this->messageModel = messageModel;
    if(messageModel)
    {
        // Report errors from message thread
        connect(messageModel, SIGNAL(error(QString,QString,bool)), this, SLOT(message(QString,QString,bool)));

        // Put transaction list in tabs
        messagePage->setModel(messageModel);

        // Balloon pop-up for new message
        connect(messageModel, SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(incomingMessage(QModelIndex,int,int)));
    }
}

void DarkSilkGUI::createTrayIcon() {
    QMenu *trayIconMenu;
#ifndef Q_OS_MAC
    trayIconMenu = new QMenu(this);

    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setContextMenu(trayIconMenu);
    trayIcon->setToolTip(tr("DarkSilk client"));
    trayIcon->setIcon(QIcon(":/icons/toolbar"));
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
    trayIcon->show();
#else
    // Note: On Mac, the dock icon is used to provide the tray's functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    dockIconHandler->setMainWindow((QMainWindow *)this);
    trayIconMenu = dockIconHandler->dockMenu();
#endif

    // Configuration of the tray icon (or dock icon) icon menu
    trayIconMenu->addAction(toggleHideAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(receiveCoinsAction);
    trayIconMenu->addAction(sendCoinsAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(signMessageAction);
    trayIconMenu->addAction(verifyMessageAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(optionsAction);
    trayIconMenu->addAction(openDEBUGConsoleAction);
#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif
    notificator = new Notificator(qApp->applicationName(), trayIcon);
}

#ifndef Q_OS_MAC
void DarkSilkGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason) 
{
    if(reason == QSystemTrayIcon::Trigger) 
    {
        // Click on system tray icon triggers show/hide of the main window
        toggleHideAction->trigger();
    }
}
#endif

void DarkSilkGUI::unlockIconClicked() 
{
    if(!walletModel) 
    {
        return;
    }

    WalletModel::EncryptionStatus encryptionStatus = walletModel->getEncryptionStatus();

    if (encryptionStatus == WalletModel::Locked) 
    {
        AskPassphraseDialog::Mode mode = sender() == labelEncryptionIcon
                                         ? AskPassphraseDialog::UnlockStaking : AskPassphraseDialog::Unlock;


        AskPassphraseDialog dlg(mode, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
}

void DarkSilkGUI::lockIconClicked() 
{
    if(!walletModel) 
    {
        return;
    }

    WalletModel::EncryptionStatus encryptionStatus = walletModel->getEncryptionStatus();

    if (encryptionStatus == WalletModel::Unlocked || fWalletUnlockStakingOnly) 
    {
        walletModel->setWalletLocked(true);
        fWalletUnlockStakingOnly = false;
    }

    setEncryptionStatus(walletModel->getEncryptionStatus());
}

void DarkSilkGUI::optionsClicked() 
{
    if(!clientModel || !clientModel->getOptionsModel()) 
    {
        return;
    }

    OptionsDialog dlg;
    dlg.setModel(clientModel->getOptionsModel());
#ifdef USE_NATIVE_I2P
    dlg.setClientModel(clientModel);
#endif
    dlg.exec();
}

void DarkSilkGUI::aboutClicked() 
{
    AboutDialog dlg;

    dlg.setModel(clientModel);
    dlg.exec();
}

#ifdef USE_NATIVE_I2P
void DarkSilkGUI::showGeneratedI2PAddr(const QString& caption, const QString& pub, const QString& priv, const QString& b32, const QString& configFileName) {
    ShowI2PAddresses i2pDialog(caption, pub, priv, b32, configFileName, this);
    i2pDialog.exec();
}
#endif

#ifdef USE_NATIVE_I2P
void DarkSilkGUI::setNumI2PConnections(int count) 
{
    QString i2pIcon;

    switch(count) 
    {
        case 0:
            i2pIcon = ":/icons/bwi2pconnect_0";
            break;

        case 1: /*case 2: case 3:*/
            i2pIcon = ":/icons/bwi2pconnect_1";
            break;

        case 2:/*case 4: case 5: case 6:*/
            i2pIcon = ":/icons/bwi2pconnect_2";
            break;

        case 3:/*case 7: case 8: case 9:*/
            i2pIcon = ":/icons/bwi2pconnect_3";
            break;

        default:
            i2pIcon = ":/icons/bwi2pconnect_4";
            break;
    }

    labelI2PConnections->setPixmap(QPixmap(i2pIcon));
    labelI2PConnections->setToolTip(tr("%n active connection(s) to I2P-DarkSilk network", "", count));
}
#endif

void DarkSilkGUI::setNumConnections(int count) 
{
    QString icon;
    switch(count) 
    {
        case 0: icon = fUseBlackTheme ? ":/icons/black/connect_0" : ":/icons/connect_0"; break;
        case 1: case 2: case 3: icon = fUseBlackTheme ? ":/icons/black/connect_1" : ":/icons/connect_1"; break;
        case 4: case 5: case 6: icon = fUseBlackTheme ? ":/icons/black/connect_2" : ":/icons/connect_2"; break;
        case 7: case 8: case 9: icon = fUseBlackTheme ? ":/icons/black/connect_3" : ":/icons/connect_3"; break;
        default: icon = fUseBlackTheme ? ":/icons/black/connect_4" : ":/icons/connect_4"; break;
    }

    labelConnectionsIcon->setPixmap(QIcon(icon).pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
    labelConnectionsIcon->setToolTip(tr("%n active connection(s) to DarkSilk network", "", count));
}

void DarkSilkGUI::setNumBlocks(int count) 
{
    // don't show / hide progress bar and its label if we have no connection to the network
    if (!clientModel || (clientModel->getNumConnections() == 0 && !clientModel->isImporting()))
    {
        statusBar()->setVisible(false);

        return;
    }

    QString tooltip;

    QDateTime lastBlockDate = clientModel->getLastBlockDate();
    QDateTime currentDate = QDateTime::currentDateTime();
    int totalSecs = GetTime() - Params().GenesisBlock().GetBlockTime();
    int secs = lastBlockDate.secsTo(currentDate);

    tooltip = tr("Processed %1 blocks of transaction history.").arg(count);

    // Set icon state: spinning if catching up, tick otherwise
    if(secs < 25 * 60) {
        tooltip = tr("Up to date") + QString(".<br>") + tooltip;

        labelBlocksIcon->setPixmap(QIcon(fUseBlackTheme ? ":/icons/black/synced" : ":/icons/synced").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
        overviewPage->showOutOfSyncWarning(false);
        progressBarLabel->setVisible(false);
        progressBar->setVisible(false);
    } else {
        // Represent time from last generated block in human readable text
        QString timeBehindText;
        const int HOUR_IN_SECONDS = 60 * 60;
        const int DAY_IN_SECONDS = 24 * 60 * 60;
        const int WEEK_IN_SECONDS = 7 * 24 * 60 * 60;
        const int YEAR_IN_SECONDS = 31556952; // Average length of year in Gregorian calendar

        if(secs < 2 * DAY_IN_SECONDS) {
            timeBehindText = tr("%n hour(s)", "", secs / HOUR_IN_SECONDS);
        } else if(secs < 2 * WEEK_IN_SECONDS) {
            timeBehindText = tr("%n day(s)", "", secs / DAY_IN_SECONDS);
        } else if(secs < YEAR_IN_SECONDS) {
            timeBehindText = tr("%n week(s)", "", secs / WEEK_IN_SECONDS);
        } else {
            int years = secs / YEAR_IN_SECONDS;
            int remainder = secs % YEAR_IN_SECONDS;

            timeBehindText = tr("%1 and %2").arg(tr("%n year(s)", "", years)).arg(tr("%n week(s)", "", remainder / WEEK_IN_SECONDS));
        }

        progressBarLabel->setText(tr(clientModel->isImporting() ? "Importing blocks..." : "Synchronizing with network..."));
        progressBarLabel->setVisible(true);

        progressBar->setFormat(tr("%1 behind").arg(timeBehindText));
        progressBar->setMaximum(totalSecs);
        progressBar->setValue(totalSecs - secs);
        progressBar->setVisible(true);
        //fShowStatusBar = true;

        tooltip = tr("Catching up...") + QString("<br>") + tooltip;

        labelBlocksIcon->setMovie(syncIconMovie);

        if(count != prevBlocks) 
        {
            syncIconMovie->jumpToNextFrame();
        }

        prevBlocks = count;

        overviewPage->showOutOfSyncWarning(true);

        tooltip += QString("<br>");
        tooltip += tr("Last received block was generated %1 ago.").arg(timeBehindText);
        tooltip += QString("<br>");
        tooltip += tr("Transactions after this will not yet be visible.");
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);

    statusBar()->setVisible(true);
}

void DarkSilkGUI::message(const QString &title, const QString &message, bool modal, unsigned int style, bool *ret) 
{   
    
    QString strTitle = tr("DarkSilk Core") + " - ";
    // Default to information icon
    int nMBoxIcon = QMessageBox::Information;
    int nNotifyIcon = Notificator::Information;

    // Check for usage of predefined title
    switch (style) 
    {
        case CClientUIInterface::MSG_ERROR:
            strTitle += tr("Error");
            break;

        case CClientUIInterface::MSG_WARNING:
            strTitle += tr("Warning");
            break;

        case CClientUIInterface::MSG_INFORMATION:
            strTitle += tr("Information");
            break;

        default:
            strTitle += title; // Use supplied title
    }

    // Check for error/warning icon
    if (style & CClientUIInterface::ICON_ERROR) 
    {
        nMBoxIcon = QMessageBox::Critical;
        nNotifyIcon = Notificator::Critical;
    } else if (style & CClientUIInterface::ICON_WARNING) 
    {
        nMBoxIcon = QMessageBox::Warning;
        nNotifyIcon = Notificator::Warning;
    }
    
    // Display message
    if (modal) {
        // Check for buttons, use OK as default, if none was supplied
        QMessageBox::StandardButton buttons;

        if (!(buttons = (QMessageBox::StandardButton)(style & CClientUIInterface::BTN_MASK))) 
        {
            buttons = QMessageBox::Ok;
        }

        // Ensure we get users attention, but only if main window is visible
        // as we don't want to pop up the main window for messages that happen before
        // initialization is finished.
        if(!(CClientUIInterface::NOSHOWGUI))
            showNormalIfMinimized();
        QMessageBox mBox((QMessageBox::Icon)nMBoxIcon, strTitle, message, buttons, this);
        int r = mBox.exec();
        if (ret != NULL)
            *ret = r == QMessageBox::Ok;
    } else {
        notificator->notify((Notificator::Class)nNotifyIcon, strTitle, message);
    }
}

void DarkSilkGUI::changeEvent(QEvent *e) 
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
}

void DarkSilkGUI::closeEvent(QCloseEvent *event) 
{
#ifndef Q_OS_MAC // Ignored on Mac
    if(clientModel&& clientModel->getOptionsModel())
         {
        if(!clientModel->getOptionsModel()->getMinimizeOnClose()) 
        {
            qApp->quit();
        }
    }

#endif

    QMainWindow::closeEvent(event);
}

void DarkSilkGUI::askFee(CAmount nFeeRequired, bool *payFee) 
{
    if (!clientModel || !clientModel->getOptionsModel()) 
    {
        return;
    }

    QString strMessage = tr("This transaction is over the size limit. You can still send it for a fee of %1, "
                            "which goes to the nodes that process your transaction and helps to support the network. "
                            "Do you want to pay the fee?").arg(DarkSilkUnits::formatWithUnit(clientModel->getOptionsModel()->getDisplayUnit(), nFeeRequired));

    QMessageBox::StandardButton retval = QMessageBox::question(
            this, tr("Confirm transaction fee"), strMessage,
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);

    *payFee = (retval == QMessageBox::Yes);
}

void DarkSilkGUI::incomingTransaction(const QModelIndex & parent, int start, int end) 
{
    if(!walletModel || !clientModel) 
    {
        return;
    }

    TransactionTableModel *ttm = walletModel->getTransactionTableModel();
    qint64 amount = ttm->index(start, TransactionTableModel::Amount, parent)
                    .data(Qt::EditRole).toULongLong();

    if(!clientModel->inInitialBlockDownload()) {
        // On new transaction, make an info balloon
        // Unless the initial block download is in progress, to prevent balloon-spam
        QString date = ttm->index(start, TransactionTableModel::Date, parent)
                       .data().toString();

        QString type = ttm->index(start, TransactionTableModel::Type, parent)
                       .data().toString();

        QString address = ttm->index(start, TransactionTableModel::ToAddress, parent)
                          .data().toString();

        QIcon icon = qvariant_cast<QIcon>(ttm->index(start,
                                          TransactionTableModel::ToAddress, parent)
                                          .data(Qt::DecorationRole));

        notificator->notify(Notificator::Information,
                            (amount) < 0 ? tr("Sent transaction") :
                            tr("Incoming transaction"),
                            tr("Date: %1\n"
                               "Amount: %2\n"
                               "Type: %3\n"
                               "Address: %4\n")
                            .arg(date)
                            .arg(DarkSilkUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), amount, true))
                            .arg(type)
                            .arg(address), icon);
    }
}

void DarkSilkGUI::incomingMessage(const QModelIndex & parent, int start, int end)
{
    if(!messageModel)
        return;

    MessageModel *mm = messageModel;

    if (mm->index(start, MessageModel::TypeInt, parent).data().toInt() == MessageTableEntry::Received)
    {
        QString sent_datetime = mm->index(start, MessageModel::ReceivedDateTime, parent).data().toString();
        QString from_address  = mm->index(start, MessageModel::FromAddress,      parent).data().toString();
        QString to_address    = mm->index(start, MessageModel::ToAddress,        parent).data().toString();
        QString message       = mm->index(start, MessageModel::Message,          parent).data().toString();
        QTextDocument html;
        html.setHtml(message);
        QString messageText(html.toPlainText());
        notificator->notify(Notificator::Information,
                            tr("Incoming Message"),
                            tr("Date: %1\n"
                               "From Address: %2\n"
                               "To Address: %3\n"
                               "Message: %4\n")
                              .arg(sent_datetime)
                              .arg(from_address)
                              .arg(to_address)
                              .arg(messageText));
    };
}

void DarkSilkGUI::gotoOverviewPage() 
{
    overviewAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(overviewPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void DarkSilkGUI::gotoHistoryPage() 
{
    historyAction->setChecked(true);    
    centralStackedWidget->setCurrentWidget(transactionsPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), transactionView, SLOT(exportClicked()));
}

void DarkSilkGUI::gotoAddressBookPage() 
{
    addressBookAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(addressBookPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), addressBookPage, SLOT(exportClicked()));
}

void DarkSilkGUI::gotoReceiveCoinsPage() 
{
    receiveCoinsAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(receiveCoinsPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), receiveCoinsPage, SLOT(exportClicked()));
}

void DarkSilkGUI::gotoSendCoinsPage() 
{
    sendCoinsAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(sendCoinsPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void DarkSilkGUI::gotoSignMessageTab(QString addr) {
    // call show() in showTab_SM()
    signVerifyMessageDialog->showTab_SM(true);

    if(!addr.isEmpty()) {
        signVerifyMessageDialog->setAddress_SM(addr);
    }
}

void DarkSilkGUI::gotoVerifyMessageTab(QString addr) 
{
    // call show() in showTab_VM()
    signVerifyMessageDialog->showTab_VM(true);

    if(!addr.isEmpty()) {
        signVerifyMessageDialog->setAddress_VM(addr);
    }
}
/*
void DarkSilkGUI::gotoBlockBrowser() 
{
    blockAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(blockBrowser);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void DarkSilkGUI::gotoStatisticsPage() 
{
    statisticsAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(statisticsPage);

    statisticsPage->updateStatistics();

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}
*/
void DarkSilkGUI::gotoMessagePage()
{
    messageAction->setChecked(true);      
    centralStackedWidget->setCurrentWidget(messagePage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), messagePage, SLOT(exportClicked()));
}

void DarkSilkGUI::gotoStormnodeManagerPage() 
{

    if(!stormnodeManagerPage)
    {
        stormnodeManagerPage = new StormnodeManager(this);
        centralStackedWidget->addWidget(stormnodeManagerPage);
    }

    stormnodeManagerAction->setChecked(true); 
    centralStackedWidget->setCurrentWidget(stormnodeManagerPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void DarkSilkGUI::dragEnterEvent(QDragEnterEvent *event) 
{
    // Accept only URIs
    if(event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void DarkSilkGUI::dropEvent(QDropEvent *event) 
{
    if(event->mimeData()->hasUrls()) {
        int nValidUrisFound = 0;
        QList<QUrl> uris = event->mimeData()->urls();
        foreach(const QUrl & uri, uris) {
            if (sendCoinsPage->handleURI(uri.toString())) {
                nValidUrisFound++;
            }
        }

        // if valid URIs were found
        if (nValidUrisFound) {
            gotoSendCoinsPage();
        } else {
            notificator->notify(Notificator::Warning, tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid DarkSilk address or malformed URI parameters."));
        }
    }

    event->acceptProposedAction();
}

void DarkSilkGUI::handleURI(QString strURI) 
{
    // URI has to be valid
    if (sendCoinsPage->handleURI(strURI)) {
        showNormalIfMinimized();
        gotoSendCoinsPage();
    } else {
        notificator->notify(Notificator::Warning, tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid DarkSilk address or malformed URI parameters."));
    }
}

void DarkSilkGUI::setEncryptionStatus(int status) 
{
    disconnect(labelEncryptionIcon, SIGNAL(clicked()), this, SLOT(unlockIconClicked()));
    disconnect(labelEncryptionIcon, SIGNAL(clicked()), this, SLOT(lockIconClicked()));

    if(fWalletUnlockStakingOnly) {
        labelEncryptionIcon->setPixmap(QIcon(fUseBlackTheme ? ":/icons/black/lock_open" : ":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked for staking only</b>"));

        changePassphraseAction->setEnabled(false);

        unlockWalletAction->setVisible(false);

        lockWalletAction->setVisible(true);

        encryptWalletAction->setEnabled(false);

        connect(labelEncryptionIcon, SIGNAL(clicked()), this, SLOT(lockIconClicked()));
    } else {
        switch(status) {
            case WalletModel::Unencrypted:
                labelEncryptionIcon->setPixmap(QIcon(fUseBlackTheme ? ":/icons/black/lock_open" : ":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
                labelEncryptionIcon->setToolTip(tr("Wallet is <b>not encrypted</b>"));

                changePassphraseAction->setEnabled(false);

                unlockWalletAction->setVisible(false);

                lockWalletAction->setVisible(false);

                encryptWalletAction->setEnabled(true);
                break;

            case WalletModel::Unlocked:
                labelEncryptionIcon->setPixmap(QIcon(fUseBlackTheme ? ":/icons/black/lock_open" : ":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
                labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>"));

                changePassphraseAction->setEnabled(true);

                unlockWalletAction->setVisible(false);

                lockWalletAction->setVisible(true);

                encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported

                connect(labelEncryptionIcon, SIGNAL(clicked()), this, SLOT(lockIconClicked()));
                break;

            case WalletModel::Locked:
                labelEncryptionIcon->setPixmap(QIcon(fUseBlackTheme ? ":/icons/black/lock_closed" : ":/icons/lock_closed").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
                labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>locked</b>"));

                changePassphraseAction->setEnabled(true);

                unlockWalletAction->setVisible(true);

                lockWalletAction->setVisible(false);

                encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported

                connect(labelEncryptionIcon, SIGNAL(clicked()), this, SLOT(unlockIconClicked()));
                break;
        }

    }
}

void DarkSilkGUI::encryptWallet() 
{
    if(!walletModel) {
        return;
    }

    AskPassphraseDialog dlg(AskPassphraseDialog::Encrypt, this);

    dlg.setModel(walletModel);
    dlg.exec();

    setEncryptionStatus(walletModel->getEncryptionStatus());
}

void DarkSilkGUI::backupWallet() {
    QString saveDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
    QString filename = QFileDialog::getSaveFileName(this, tr("Backup Wallet"), saveDir, tr("Wallet Data (*.dat)"));

    if(!filename.isEmpty()) {
        if(!walletModel->backupWallet(filename)) {
            QMessageBox::warning(this, tr("Backup Failed"), tr("There was an error trying to save the wallet data to the new location."));
        }
    }
}

void DarkSilkGUI::changePassphrase() 
{
    AskPassphraseDialog dlg(AskPassphraseDialog::ChangePass, this);

    dlg.setModel(walletModel);
    dlg.exec();
}

void DarkSilkGUI::unlockWallet() 
{
    if(!walletModel) {
        return;
    }

    WalletModel::EncryptionStatus encryptionStatus = walletModel->getEncryptionStatus();

    if (encryptionStatus == WalletModel::Locked) {
        AskPassphraseDialog::Mode mode = sender() == unlockWalletAction
                                         ? AskPassphraseDialog::UnlockStaking : AskPassphraseDialog::Unlock;

        AskPassphraseDialog dlg(mode, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
}

void DarkSilkGUI::lockWallet() 
{
    if(!walletModel) {
        return;
    }

    WalletModel::EncryptionStatus encryptionStatus = walletModel->getEncryptionStatus();

    if (encryptionStatus == WalletModel::Unlocked || fWalletUnlockStakingOnly) {
        walletModel->setWalletLocked(true);
        fWalletUnlockStakingOnly = false;
    }

    setEncryptionStatus(walletModel->getEncryptionStatus());
}

void DarkSilkGUI::showNormalIfMinimized(bool fToggleHidden) 
{
    // activateWindow() (sometimes) helps with keyboard focus on Windows
    if (isHidden()) {
        show();
        activateWindow();
    } else if (isMinimized()) {
        showNormal();
        activateWindow();
    } else if (GUIUtil::isObscured(this)) {
        raise();
        activateWindow();
    } else if(fToggleHidden) {
        hide();
    }
}

void DarkSilkGUI::toggleHidden() 
{
    showNormalIfMinimized(true);
}

void DarkSilkGUI::updateWeight() 
{
    if (!pwalletMain) {
        return;
    }

    TRY_LOCK(cs_main, lockMain);

    if (!lockMain) {
        return;
    }

    TRY_LOCK(pwalletMain->cs_wallet, lockWallet);

    if (!lockWallet) {
        return;
    }

    nWeight = pwalletMain->GetStakeWeight();
}

void DarkSilkGUI::updateStakingIcon() 
{
    updateWeight();

    if (pindexBest != NULL && nLastCoinStakeSearchInterval && nWeight &&
            pindexBest->nHeight >= Params().FirstPOSBlock()) {
        uint64_t nWeight = this->nWeight;
        uint64_t nNetworkWeight = GetPoSKernelPS();

        unsigned nEstimateTime = POS_TARGET_SPACING * nNetworkWeight / nWeight;

        QString text;

        if (nEstimateTime < 60) {
            text = tr("%n second(s)", "", nEstimateTime);
        } else if (nEstimateTime < 60 * 60) {
            text = tr("%n minute(s)", "", nEstimateTime / 60);
        } else if (nEstimateTime < 24 * 60 * 60) {
            text = tr("%n hour(s)", "", nEstimateTime / (60 * 60));
        } else {
            text = tr("%n day(s)", "", nEstimateTime / (60 * 60 * 24));
        }

        nWeight /= COIN;
        nNetworkWeight /= COIN;

        labelStakingIcon->setPixmap(QIcon(fUseBlackTheme ? ":/icons/black/staking_on" : ":/icons/staking_on").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
        labelStakingIcon->setToolTip(tr("Staking.<br>Your weight is %1<br>Network weight is %2<br>Expected time to earn reward is %3").arg(nWeight).arg(nNetworkWeight).arg(text));
    } else {
        labelStakingIcon->setPixmap(QIcon(fUseBlackTheme ? ":/icons/black/staking_off" : ":/icons/staking_off").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));

        if (pindexBest != NULL && pindexBest->nHeight < Params().FirstPOSBlock()) {
            labelStakingIcon->setToolTip(tr("Not staking because proof-of-stake has not started"));
        } else if (pwalletMain && pwalletMain->IsLocked()) {
            labelStakingIcon->setToolTip(tr("Not staking because wallet is locked"));
        } else if (vNodes.empty()) {
            labelStakingIcon->setToolTip(tr("Not staking because wallet is offline"));
        } else if (IsInitialBlockDownload()) {
            labelStakingIcon->setToolTip(tr("Not staking because wallet is syncing"));
        } else if (!nWeight) {
            labelStakingIcon->setToolTip(tr("Not staking because you don't have mature coins"));
        } else {
            labelStakingIcon->setToolTip(tr("Not staking"));
        }
    }
}


void DarkSilkGUI::showProgress(const QString &title, int nProgress)
{
    if (nProgress == 0)
    {
        progressDialog = new QProgressDialog(title, "", 0, 100);
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setMinimumDuration(0);
        progressDialog->setCancelButton(0);
        progressDialog->setAutoClose(false);
        progressDialog->setValue(0);
    }
    else if (nProgress == 100)
    {
        if (progressDialog)
        {
            progressDialog->close();
            progressDialog->deleteLater();
        }
    }
    else if (progressDialog)
        progressDialog->setValue(nProgress);
}

void DarkSilkGUI::detectShutdown() 
{
    if (ShutdownRequested()) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), "quit", Qt::QueuedConnection);
    }
}

WId DarkSilkGUI::getMainWinId() const 
{
    return winId();
}
