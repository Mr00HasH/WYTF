/*
 * Qt4 bitcoin GUI.
 *
 * W.J. van der Laan 2011-2012
 * The Bitcoin Developers 2011-2012
 */
#include "bitcoingui.h"
#include "transactiontablemodel.h"
#include "addressbookpage.h"
#include "sendcoinsdialog.h"
#include "signverifymessagedialog.h"
#include "optionsdialog.h"
#include "aboutdialog.h"
#include "multisenddialog.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "editaddressdialog.h"
#include "optionsmodel.h"
#include "transactiondescdialog.h"
#include "addresstablemodel.h"
#include "transactionview.h"
#include "overviewpage.h"
#include "statisticspage.h"
#include "bitcoinunits.h"
#include "guiconstants.h"
#include "askpassphrasedialog.h"
#include "notificator.h"
#include "guiutil.h"
#include "rpcconsole.h"
#include "wallet.h"
#include "blockbrowser.h"
#include "stakereportdialog.h"

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif

#include <QDebug>
#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QIcon>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QStackedWidget>
#include <QDateTime>
#include <QMovie>
#include <QFileDialog>
#include <QDesktopServices>
#include <QTimer>
#include <QDragEnterEvent>
#include <QUrl>
#include <QStyle>

#include <iostream>

extern CWallet* pwalletMain;
extern int64_t nLastCoinStakeSearchInterval;
extern unsigned int nTargetSpacing;
double GetPoSKernelPS(const CBlockIndex* blockindex = NULL);

BitcoinGUI::BitcoinGUI(QWidget *parent):
    QMainWindow(parent),
    clientModel(0),
    walletModel(0),
    encryptWalletAction(0),
    changePassphraseAction(0),
    unlockWalletAction(0),
    lockWalletAction(0),
    aboutQtAction(0),
    trayIcon(0),
    notificator(0),
    rpcConsole(0),
    blockBrowser(0),
	multisendDialog(0)
{
    updateStyle();
    resize(1000, 600);
    setWindowTitle(tr("WYTF [WYTF]") + " - " + (QString::fromStdString(FormatFullVersion()).split("-")[0]));
#ifndef Q_OS_MAC
    qApp->setWindowIcon(QIcon(":icons/bitcoin"));
    setWindowIcon(QIcon(":icons/bitcoin"));
#else
    setUnifiedTitleAndToolBarOnMac(true);
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

    // Create tabs
    overviewPage = new OverviewPage();
	statisticsPage = new StatisticsPage(this);
    blockBrowser = new BlockBrowser(this);
    transactionsPage = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout();
    transactionView = new TransactionView(this);
    vbox->addWidget(transactionView);
    transactionsPage->setLayout(vbox);

    addressBookPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::SendingTab);

    receiveCoinsPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::ReceivingTab);

    sendCoinsPage = new SendCoinsDialog(this);
	
	multisendDialog = new MultisendDialog(this);
    
    signVerifyMessageDialog = new SignVerifyMessageDialog(this);

    centralWidget = new QStackedWidget(this);
    centralWidget->addWidget(overviewPage);
    centralWidget->addWidget(transactionsPage);
    centralWidget->addWidget(addressBookPage);
    centralWidget->addWidget(receiveCoinsPage);
    centralWidget->addWidget(sendCoinsPage);
	centralWidget->addWidget(statisticsPage);
	centralWidget->addWidget(multisendDialog);
    centralWidget->addWidget(blockBrowser);
    setCentralWidget(centralWidget);

     // Create status bar
    statusBar(); 

    // Status bar notification icons
    QFrame *frameBlocks = new QFrame();
    frameBlocks->setContentsMargins(0,0,0,0);
    frameBlocks->setMinimumWidth(98);
    frameBlocks->setMaximumWidth(98);
    QHBoxLayout *frameBlocksLayout = new QHBoxLayout(frameBlocks);
    frameBlocksLayout->setContentsMargins(0,0,0,0);
    frameBlocksLayout->setSpacing(3);
    labelEncryptionIcon = new QLabel();
	labelStakingIcon = new QLabel();
    labelConnectionsIcon = new QLabel();
    labelBlocksIcon = new QLabel();
	
	if (GetBoolArg("-staking", true))
    {
        QTimer *timerStakingIcon = new QTimer(labelStakingIcon);
        connect(timerStakingIcon, SIGNAL(timeout()), this, SLOT(updateStakingIcon()));
        timerStakingIcon->start(30 * 1000);
        updateStakingIcon();
    }

    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelEncryptionIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelStakingIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelConnectionsIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelBlocksIcon);
    frameBlocksLayout->addStretch();

    // Progress bar and label for blocks download
    progressBarLabel = new QLabel();
    progressBarLabel->setVisible(false);
    progressBar = new QProgressBar();
    progressBar->setAlignment(Qt::AlignCenter);
    progressBar->setVisible(false);

    // Override style sheet for progress bar for styles that have a segmented progress bar,
    // as they make the text unreadable (workaround for issue #1071)
    // See https://qt-project.org/doc/qt-4.8/gallery.html
    QString curStyle = qApp->style()->metaObject()->className();
    if(curStyle == "QWindowsStyle" || curStyle == "QWindowsXPStyle")
    {
        progressBar->setStyleSheet("QProgressBar { background-color: #e8e8e8; border: 1px solid grey; border-radius: 7px; padding: 1px; text-align: center; } QProgressBar::chunk { background: QLinearGradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #FF8000, stop: 1 orange); border-radius: 7px; margin: 0px; }");
    }

    statusBar()->addWidget(progressBarLabel);
    statusBar()->addWidget(progressBar);
    statusBar()->addPermanentWidget(frameBlocks);

    syncIconMovie = new QMovie(":/movies/update_spinner", "png", this);
    stakingOnMovie = new QMovie(":/movies/staking_on", "png", this);

    // Clicking on a transaction on the overview page simply sends you to transaction history page
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), this, SLOT(gotoHistoryPage()));
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), transactionView, SLOT(focusTransaction(QModelIndex)));

    // Double-clicking on a transaction on the transaction history page shows details
    connect(transactionView, SIGNAL(doubleClicked(QModelIndex)), transactionView, SLOT(showDetails()));

    rpcConsole = new RPCConsole(this);
    connect(openRPCConsoleAction, SIGNAL(triggered()), rpcConsole, SLOT(showTab_Debug()));
	
    blockBrowser = new BlockBrowser(this);
    connect(blockAction, SIGNAL(triggered()), blockBrowser, SLOT(show()));
	
	multisendDialog = new MultisendDialog(this);
    connect(multisendAction, SIGNAL(triggered()), multisendDialog, SLOT(show()));

    // Clicking on "Verify Message" in the address book sends you to the verify message tab
    connect(addressBookPage, SIGNAL(verifyMessage(QString)), this, SLOT(gotoVerifyMessageTab(QString)));
    // Clicking on "Sign Message" in the receive coins page sends you to the sign message tab
    connect(receiveCoinsPage, SIGNAL(signMessage(QString)), this, SLOT(gotoSignMessageTab(QString)));
    // Clicking on "Block Explorer" in the transaction page sends you to the blockbrowser
    connect(transactionView, SIGNAL(blockBrowserSignal(QString)), this, SLOT(gotoBlockBrowser(QString)));
	// Clicking on stake for multisend button in the address book sends you to the multisend page
    connect(addressBookPage, SIGNAL(multisendSignal(QString)), this, SLOT(multisendClicked(QString)));

    gotoOverviewPage();
}

BitcoinGUI::~BitcoinGUI()
{
    if(trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
        trayIcon->hide();
#ifdef Q_OS_MAC
    delete appMenuBar;
#endif
}

void BitcoinGUI::createActions()
{
    QActionGroup *tabGroup = new QActionGroup(this);

    overviewAction = new QAction(QIcon(":/icons/overview2"), tr("&Overview"), this);
    overviewAction->setToolTip(tr("Show general overview of your wallet"));
    overviewAction->setCheckable(true);
    overviewAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_1));
    tabGroup->addAction(overviewAction);	
	
    sendCoinsAction = new QAction(QIcon(":/icons/send2"), tr("&Send"), this);
    sendCoinsAction->setToolTip(tr("Send coins to a WYTF address"));
    sendCoinsAction->setCheckable(true);
    sendCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_2));
    tabGroup->addAction(sendCoinsAction);

    receiveCoinsAction = new QAction(QIcon(":/icons/receiving_addresses2"), tr("&Receive"), this);
    receiveCoinsAction->setToolTip(tr("Show the list of addresses for receiving payments"));
    receiveCoinsAction->setCheckable(true);
    receiveCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_3));
    tabGroup->addAction(receiveCoinsAction);

    historyAction = new QAction(QIcon(":/icons/history2"), tr("&Transactions"), this);
    historyAction->setToolTip(tr("Browse transaction history"));
    historyAction->setCheckable(true);
    historyAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_4));
    tabGroup->addAction(historyAction);

    addressBookAction = new QAction(QIcon(":/icons/address-book2"), tr("&Address Book"), this);
    addressBookAction->setToolTip(tr("Edit the list of stored addresses and labels"));
    addressBookAction->setCheckable(true);
    addressBookAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_5));
    tabGroup->addAction(addressBookAction);
	
    statisticsAction = new QAction(QIcon(":/icons/statistics"), tr("&Statistics"), this);
    statisticsAction->setToolTip(tr("View statistics"));
    statisticsAction->setCheckable(true);
	statisticsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_6));
    tabGroup->addAction(statisticsAction);	
	   
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(gotoOverviewPage()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(gotoHistoryPage()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(gotoAddressBookPage()));		
    
    quitAction = new QAction(QIcon(":/icons/quit"), tr("E&xit"), this);
    quitAction->setStatusTip(tr("Quit application"));
    quitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);
    aboutAction = new QAction(QIcon(":/icons/bitcoin"), tr("&About WYTF"), this);
    aboutAction->setStatusTip(tr("Show information about WYTF"));
    aboutAction->setMenuRole(QAction::AboutRole);
    aboutQtAction = new QAction(QIcon(":/icons/qt"), tr("About &Qt"), this);
    aboutQtAction->setStatusTip(tr("Show information about Qt"));
    aboutQtAction->setMenuRole(QAction::AboutQtRole);
    optionsAction = new QAction(QIcon(":/icons/options"), tr("&Options..."), this);
    optionsAction->setStatusTip(tr("Modify configuration options for WYTF"));
    optionsAction->setMenuRole(QAction::PreferencesRole);
    toggleHideAction = new QAction(QIcon(":/icons/bitcoin"), tr("&Show / Hide"), this);
    encryptWalletAction = new QAction(QIcon(":/icons/lock_closed"), tr("&Encrypt Wallet..."), this);
    encryptWalletAction->setStatusTip(tr("Encrypt or decrypt wallet"));
    encryptWalletAction->setCheckable(true);
    backupWalletAction = new QAction(QIcon(":/icons/filesave"), tr("&Backup Wallet..."), this);
    backupWalletAction->setStatusTip(tr("Backup wallet to another location"));
    changePassphraseAction = new QAction(QIcon(":/icons/key"), tr("&Change Passphrase..."), this);
    changePassphraseAction->setStatusTip(tr("Change the passphrase used for wallet encryption"));
    unlockWalletAction = new QAction(QIcon(":/icons/lock_open"), tr("&Unlock Wallet..."), this);
    unlockWalletAction->setStatusTip(tr("Unlock wallet"));
    lockWalletAction = new QAction(QIcon(":/icons/lock_closed"), tr("&Lock Wallet"), this);
    lockWalletAction->setStatusTip(tr("Lock wallet"));
    signMessageAction = new QAction(QIcon(":/icons/edit"), tr("Sign &message..."), this);
    verifyMessageAction = new QAction(QIcon(":/icons/verify"), tr("&Verify message..."), this);
	checkWalletAction = new QAction(QIcon(":/icons/checkwallet"), tr("&Check Wallet..."), this);
	checkWalletAction->setStatusTip(tr("Check wallet integrity and report findings"));
	repairWalletAction = new QAction(QIcon(":/icons/repairwallet"), tr("&Repair Wallet..."), this);
	repairWalletAction->setStatusTip(tr("Fix wallet integrity and remove orphans"));	
	stakeReportAction = new QAction(QIcon(":/icons/stakejournal"), tr("Stake Report"), this);
    stakeReportAction->setToolTip(tr("Open the Stake Report Box"));	
    exportAction = new QAction(QIcon(":/icons/export2"), tr("&Export..."), this);
    exportAction->setStatusTip(tr("Export the data in the current tab to a file"));
    openRPCConsoleAction = new QAction(QIcon(":/icons/debugwindow2"), tr("&Debug Window"), this);
    openRPCConsoleAction->setStatusTip(tr("Open debugging and diagnostic console"));	
    blockAction = new QAction(QIcon(":/icons/blockbrowser2"), tr("&Block Browser"), this);
    blockAction->setToolTip(tr("Explore the BlockChain"));	
	multisendAction = new QAction(QIcon(":/icons/multisend"), tr("&MultiSend"), this);
    multisendAction->setToolTip(tr("MultiSend Settings"));	
	resourcesWYTFAction = new QAction(QIcon(":/icons/web-main"), tr("&wytf.co"), this);
	resourcesWYTFAction->setToolTip(tr("Visit the Official WYTF website"));
	resourcesCHAINAction = new QAction(QIcon(":/icons/web-chain"), tr("&Blockchain Explorer"), this);
	resourcesCHAINAction->setToolTip(tr("Visit the Official WYTF Blockchain Explorer"));				
	resourcesTWITTERAction = new QAction(QIcon(":/icons/web-twitter"), tr("&Twitter"), this);
	resourcesTWITTERAction->setToolTip(tr("Visit the Official WYTF Twitter"));
	resourcesBTCTAction = new QAction(QIcon(":/icons/web-btct"), tr("&Discussion Forum"), this);
	resourcesBTCTAction->setToolTip(tr("Visit the WYTF discussion forum on bitcointalk.org"));
	resourcesYOBITAction = new QAction(QIcon(":/icons/web-yobit"), tr("&Yobit.net - WYTF/BTC"), this);
	resourcesYOBITAction->setToolTip(tr("Visit the WYTF market on Yobit.net"));

    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(aboutClicked()));
    connect(aboutQtAction, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(optionsAction, SIGNAL(triggered()), this, SLOT(optionsClicked()));
    connect(toggleHideAction, SIGNAL(triggered()), this, SLOT(toggleHidden()));
    connect(encryptWalletAction, SIGNAL(triggered(bool)), this, SLOT(encryptWallet(bool)));
	connect(checkWalletAction, SIGNAL(triggered()), this, SLOT(checkWallet()));
	connect(repairWalletAction, SIGNAL(triggered()), this, SLOT(repairWallet()));
    connect(backupWalletAction, SIGNAL(triggered()), this, SLOT(backupWallet()));
    connect(changePassphraseAction, SIGNAL(triggered()), this, SLOT(changePassphrase()));
    connect(unlockWalletAction, SIGNAL(triggered()), this, SLOT(unlockWallet()));
    connect(lockWalletAction, SIGNAL(triggered()), this, SLOT(lockWallet()));
    connect(signMessageAction, SIGNAL(triggered()), this, SLOT(gotoSignMessageTab()));
    connect(verifyMessageAction, SIGNAL(triggered()), this, SLOT(gotoVerifyMessageTab()));
	connect(statisticsAction, SIGNAL(triggered()), this, SLOT(gotoStatisticsPage()));
    connect(blockAction, SIGNAL(triggered()), this, SLOT(gotoBlockBrowser()));
	connect(stakeReportAction, SIGNAL(triggered()), this, SLOT(stakeReportClicked()));
	connect(resourcesWYTFAction, SIGNAL(triggered()), this, SLOT(resourcesWYTFClicked()));
	connect(resourcesCHAINAction, SIGNAL(triggered()), this, SLOT(resourcesCHAINClicked()));;
	connect(resourcesTWITTERAction, SIGNAL(triggered()), this, SLOT(resourcesTWITTERClicked()));		
	connect(resourcesBTCTAction, SIGNAL(triggered()), this, SLOT(resourcesBTCTClicked()));
	connect(resourcesYOBITAction, SIGNAL(triggered()), this, SLOT(resourcesYOBITClicked()));

}

void BitcoinGUI::createMenuBar()
{
#ifdef Q_OS_MAC
    // Create a decoupled menu bar on Mac which stays even if the window is closed
    appMenuBar = new QMenuBar();
#else
    // Get the main window's menu bar on other platforms
    appMenuBar = menuBar();
#endif

    // Configure the menus
    QMenu *file = appMenuBar->addMenu(tr("&File"));
    file->addAction(backupWalletAction);
    file->addAction(exportAction);
    file->addSeparator();	
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
	settings->addAction(checkWalletAction);
	settings->addAction(repairWalletAction);
    settings->addSeparator();
    settings->addAction(optionsAction);

    QMenu *info = appMenuBar->addMenu(tr("&Info"));
    info->addAction(openRPCConsoleAction);
    info->addSeparator();
    info->addAction(aboutAction);
    info->addAction(aboutQtAction);
	
	QMenu *resources = appMenuBar->addMenu(tr("&Resources"));
    resources->addAction(resourcesWYTFAction);
    resources->addAction(resourcesCHAINAction);
    resources->addSeparator();
    resources->addAction(resourcesTWITTERAction);
    resources->addAction(resourcesBTCTAction);
	resources->addSeparator();
	resources->addAction(resourcesYOBITAction);

	
}

static QWidget* makeToolBarSpacer()
{
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    spacer->setStyleSheet("QWidget { background: none; }");
    return spacer;
}

void BitcoinGUI::createToolBars()
{
	mainIcon = new QLabel (this);
    mainIcon->setPixmap(QPixmap(":icons/logo"));
    mainIcon->show();
	
	QToolBar *toolbar2 = addToolBar(tr("WYTF top-logo"));
	addToolBar(Qt::LeftToolBarArea,toolbar2);
    toolbar2->setOrientation(Qt::Horizontal);
    toolbar2->setFloatable(false);
    toolbar2->setMovable(false);
    toolbar2->addWidget(mainIcon);

    QToolBar *toolbar3 = addToolBar(tr("Side toolbar"));
	addToolBar(Qt::LeftToolBarArea,toolbar3);
    toolbar3->setOrientation(Qt::Vertical);
	toolbar3->setFloatable(false);
    toolbar3->setMovable(false);
    toolbar3->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	toolbar3->addAction(overviewAction);
    toolbar3->addAction(sendCoinsAction);
    toolbar3->addAction(receiveCoinsAction);
    toolbar3->addAction(historyAction);
    toolbar3->addAction(addressBookAction);
	toolbar3->addAction(statisticsAction);
	toolbar3->addAction(multisendAction);	
    toolbar3->addAction(blockAction);
   	toolbar3->addAction(stakeReportAction);	
	toolbar3->addAction(openRPCConsoleAction);
	
}

void BitcoinGUI::setClientModel(ClientModel *clientModel)
{
    this->clientModel = clientModel;
    if(clientModel)
    {
        // Replace some strings and icons, when using the testnet
        if(clientModel->isTestNet())
        {
            setWindowTitle(windowTitle() + QString(" ") + tr("[testnet]"));
#ifndef Q_OS_MAC
            qApp->setWindowIcon(QIcon(":icons/bitcoin_testnet"));
            setWindowIcon(QIcon(":icons/bitcoin_testnet"));
#else
            MacDockIconHandler::instance()->setIcon(QIcon(":icons/bitcoin_testnet"));
#endif
            if(trayIcon)
            {
                trayIcon->setToolTip(tr("WYTF client") + QString(" ") + tr("[testnet]"));
                trayIcon->setIcon(QIcon(":/icons/toolbar_testnet"));
                toggleHideAction->setIcon(QIcon(":/icons/toolbar_testnet"));
            }

            aboutAction->setIcon(QIcon(":/icons/toolbar_testnet"));
        }

        // Keep up to date with client
        setNumConnections(clientModel->getNumConnections());
        connect(clientModel, SIGNAL(numConnectionsChanged(int)), this, SLOT(setNumConnections(int)));

        setNumBlocks(clientModel->getNumBlocks(), clientModel->getNumBlocksOfPeers());
        connect(clientModel, SIGNAL(numBlocksChanged(int,int)), this, SLOT(setNumBlocks(int,int)));
        connect(clientModel, SIGNAL(numBlocksChanged(int,int)), this, SLOT(updateStakingIcon()));

        // Report errors from network/worker thread
        connect(clientModel, SIGNAL(error(QString,QString,bool)), this, SLOT(error(QString,QString,bool)));

        rpcConsole->setClientModel(clientModel);
        addressBookPage->setOptionsModel(clientModel->getOptionsModel());
        receiveCoinsPage->setOptionsModel(clientModel->getOptionsModel());
    }
}

void BitcoinGUI::setWalletModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
    if(walletModel)
    {
        // Report errors from wallet thread
        connect(walletModel, SIGNAL(error(QString,QString,bool)), this, SLOT(error(QString,QString,bool)));

        // Put transaction list in tabs
        transactionView->setModel(walletModel);

        overviewPage->setModel(walletModel);
        addressBookPage->setModel(walletModel->getAddressTableModel());
        receiveCoinsPage->setModel(walletModel->getAddressTableModel());
        sendCoinsPage->setModel(walletModel);
        signVerifyMessageDialog->setModel(walletModel);
		multisendDialog->setModel(walletModel);
		statisticsPage->setModel(clientModel);
        setEncryptionStatus(walletModel->getEncryptionStatus());
        connect(walletModel, SIGNAL(encryptionStatusChanged(int)), this, SLOT(setEncryptionStatus(int)));
        connect(walletModel, SIGNAL(encryptionStatusChanged(int)), this, SLOT(updateStakingIcon()));

        // Balloon pop-up for new transaction
        connect(walletModel->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(incomingTransaction(QModelIndex,int,int)));

        // Ask for passphrase if needed
        connect(walletModel, SIGNAL(requireUnlock()), this, SLOT(unlockWallet()));
    }
}

void BitcoinGUI::createTrayIcon()
{
    QMenu *trayIconMenu;
#ifndef Q_OS_MAC
    trayIcon = new QSystemTrayIcon(this);
    trayIconMenu = new QMenu(this);
    trayIcon->setContextMenu(trayIconMenu);
    trayIcon->setToolTip(tr("WYTF client"));
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
    trayIconMenu->addAction(overviewAction);
    trayIconMenu->addAction(sendCoinsAction);
    trayIconMenu->addAction(receiveCoinsAction);	
    trayIconMenu->addAction(historyAction);
    trayIconMenu->addAction(addressBookAction);	
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(signMessageAction);
    trayIconMenu->addAction(verifyMessageAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(optionsAction);
    trayIconMenu->addAction(openRPCConsoleAction);
#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif

    notificator = new Notificator(qApp->applicationName(), trayIcon, this);
}

#ifndef Q_OS_MAC
void BitcoinGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
    {
        // Click on system tray icon triggers show/hide of the main window
        toggleHideAction->trigger();
    }
}
#endif

double GetStrength(uint64_t nWeight)
{
    double networkWeight = GetPoSKernelPS();
    if (nWeight == 0 && networkWeight == 0)
        return 0;
    return nWeight / (static_cast<double>(nWeight) + networkWeight);
}

void BitcoinGUI::optionsClicked()
{
    if(!clientModel || !clientModel->getOptionsModel())
        return;
    OptionsDialog dlg;
    dlg.setModel(clientModel->getOptionsModel());
    dlg.exec();
}

void BitcoinGUI::aboutClicked()
{
    AboutDialog dlg;
    dlg.setModel(clientModel);
    dlg.exec();
}

void BitcoinGUI::stakeReportClicked()
{
   static StakeReportDialog dlg;
   dlg.setModel(walletModel);
   dlg.show();
}

void BitcoinGUI::resourcesWYTFClicked()
{
     QDesktopServices::openUrl(QUrl("https://wytf.co"));
}

void BitcoinGUI::resourcesCHAINClicked()
{
     QDesktopServices::openUrl(QUrl("https://explorer.wytf.co"));
}

void BitcoinGUI::resourcesTWITTERClicked()
{
     QDesktopServices::openUrl(QUrl("https://twitter.com/WYTFco"));
}

void BitcoinGUI::resourcesBTCTClicked()
{
     QDesktopServices::openUrl(QUrl("https://bitcointalk.org/index.php?topic=1511601.0"));
}

void BitcoinGUI::resourcesYOBITClicked()
{
     QDesktopServices::openUrl(QUrl("https://yobit.io/en/trade/WYTF/BTC"));
}

void BitcoinGUI::setNumConnections(int count)
{
    QString icon;
    switch(count)
    {
    case 0: icon = ":/icons/connect_0"; break;
    case 1: icon = ":/icons/connect_1"; break;
    case 2: icon = ":/icons/connect_2"; break;
    case 3: icon = ":/icons/connect_3"; break;
	case 4: icon = ":/icons/connect_4"; break;
    case 5: icon = ":/icons/connect_5"; break;
    case 6: icon = ":/icons/connect_6"; break;
	case 7: icon = ":/icons/connect_7"; break;
    default: icon = ":/icons/connect_8"; break;
    }
    labelConnectionsIcon->setPixmap(QIcon(icon).pixmap(28,54));
    labelConnectionsIcon->setToolTip(tr("WYTF Network<br><br>Connections: <b>%n</b>", "", count));
}

void BitcoinGUI::setNumBlocks(int count, int nTotalBlocks)
{
    // don't show / hide progress bar and its label if we have no connection to the network
    if (!clientModel || clientModel->getNumConnections() == 0)
    {
        progressBarLabel->setVisible(false);
        progressBar->setVisible(false);

        return;
    }

    QString strStatusBarWarnings = clientModel->getStatusBarWarnings();
    QString tooltip;

    if(count < nTotalBlocks)
    {
        int nRemainingBlocks = nTotalBlocks - count;
        float nPercentageDone = count / (nTotalBlocks * 0.01f);

        if (strStatusBarWarnings.isEmpty())
        {
            progressBarLabel->setText(tr("Status: <b>Synchronizing</b>"));
            progressBarLabel->setVisible(false);
            progressBar->setFormat(tr("Status: Synchronizing - %n block(s) remaining", "", nRemainingBlocks));
            progressBar->setMaximum(nTotalBlocks);
            progressBar->setValue(count);
            progressBar->setVisible(true);
        }

        tooltip = tr("Synchronized: <b>%1 of %2 blocks</b><br><br>Progress: <b>%3%</b>").arg(count).arg(nTotalBlocks).arg(nPercentageDone, 0, 'f', 2);
    }
    else
    {
        if (strStatusBarWarnings.isEmpty())
            progressBarLabel->setVisible(false);

        progressBar->setVisible(false);
        tooltip = tr("Synchronized: <b>%1 Blocks</b><br>").arg(count);
    }

    // Override progressBarLabel text and hide progress bar, when we have warnings to display
    if (!strStatusBarWarnings.isEmpty())
    {
        progressBarLabel->setText(strStatusBarWarnings);
         progressBarLabel->setVisible(true);
        progressBar->setVisible(false);
    }

    QDateTime lastBlockDate = clientModel->getLastBlockDate();
    int secs = lastBlockDate.secsTo(QDateTime::currentDateTime());
    QString text;

    // Represent time from last generated block in human readable text
    if(secs <= 0)
    {
        // Fully up to date. Leave text empty.
    }
    else if(secs < 60)
    {
        text = tr("%n Second(s) ago","",secs);
    }
    else if(secs < 60*60)
    {
        text = tr("%n Minute(s) ago","",secs/60);
    }
    else if(secs < 24*60*60)
    {
        text = tr("%n Hour(s) ago","",secs/(60*60));
    }
    else
    {
        text = tr("%n Day(s) ago","",secs/(60*60*24));
    }

    // Set icon state: spinning if catching up, tick otherwise
    if(secs < 90*60 && count >= nTotalBlocks)
    {
        tooltip = tr("Status: <b>Synced</b>") + QString("<br>") + tooltip;
        labelBlocksIcon->setPixmap(QIcon(":/icons/synced").pixmap(28,54));

        overviewPage->showOutOfSyncWarning(false);
    }
    else
    {
        tooltip = tr("Status: <b>Synchronizing</b>") + QString("<br>") + tooltip;
        labelBlocksIcon->setMovie(syncIconMovie);
        syncIconMovie->start();

        overviewPage->showOutOfSyncWarning(true);
    }

    if(!text.isEmpty())
    {
        tooltip += QString("<br>");
        tooltip += tr("Last Block : <b>%1</b>").arg(text);
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);

    if(GetBoolArg("-chart", true) && count > 0 && nTotalBlocks > 0)
    {
        statisticsPage->updatePlot();		
    }
	overviewPage->updateOverview();
}

void BitcoinGUI::updateStakingIcon()
{
    uint64_t nMinWeight = 0, nMaxWeight = 0, nWeight = 0;
    if (pwalletMain)
        pwalletMain->GetStakeWeight(*pwalletMain, nMinWeight, nMaxWeight, nWeight);
	if(walletModel)
		fMultiSend = pwalletMain->fMultiSend;
	
	uint64_t nNetworkWeight = GetPoSKernelPS();
    unsigned nEstimateTime = nTargetSpacing * nNetworkWeight / nWeight;

    if (pwalletMain && pwalletMain->IsLocked())
    {
        labelStakingIcon->setToolTip(tr("You are not staking:<br> <b>Your wallet is locked!</b><br><br>WYTF Network Weight: <b>%1</b><br><br>MultiSend: <b>%2</b>").arg(nNetworkWeight).arg(fMultiSend ? tr("Active"):tr("Inactive")));
        labelStakingIcon->setEnabled(false);
        labelStakingIcon->setPixmap(QIcon(":/icons/staking_off").pixmap(28,54));
    }
    else if (vNodes.empty())
    {
        labelStakingIcon->setToolTip(tr("You are not staking:<br> <b>Your wallet is offline!</b><br><br>WYTF Network Weight: <b>%1</b><br><br>MultiSend: <b>%2</b>").arg(nNetworkWeight).arg(fMultiSend ? tr("Active"):tr("Inactive")));
        labelStakingIcon->setEnabled(false);
        labelStakingIcon->setPixmap(QIcon(":/icons/staking_off").pixmap(28,54));
    }
    else if (IsInitialBlockDownload())
    {
        labelStakingIcon->setToolTip(tr("You are not staking:<br> <b>Your wallet is syncing with the WYTF network!</b><br><br>WYTF Network Weight: <b>%1</b><br><br>MultiSend: <b>%2</b>").arg(nNetworkWeight).arg(fMultiSend ? tr("Active"):tr("Inactive")));
        labelStakingIcon->setEnabled(false);
        labelStakingIcon->setPixmap(QIcon(":/icons/staking_off").pixmap(28,54));
    }
    else if (!nWeight)
    {
        labelStakingIcon->setToolTip(tr("You are not staking:<br> <b>You don't have mature coins!</b><br><br>WYTF Network Weight: <b>%1</b><br><br>MultiSend: <b>%2</b>").arg(nNetworkWeight).arg(fMultiSend ? tr("Active"):tr("Inactive")));
        labelStakingIcon->setEnabled(false);
        labelStakingIcon->setPixmap(QIcon(":/icons/staking_off").pixmap(28,54));
    }
    else if (nLastCoinStakeSearchInterval)
    {	
		uint64_t nAccuracyAdjustment = 1; // this is a manual adjustment param if needed to make more accurate
        uint64_t nEstimateTime = 60 * nNetworkWeight / nWeight / nAccuracyAdjustment;
	
		uint64_t nRangeLow = nEstimateTime;
		uint64_t nRangeHigh = nEstimateTime * 1.5;
        QString text;
        if (nEstimateTime < 60)
        {
            text = tr("%1 - %2 Seconds").arg(nRangeLow).arg(nRangeHigh);
        }
        else if (nEstimateTime < 60*60)
        {
            text = tr("%1 - %2 Minutes").arg(nRangeLow / 60).arg(nRangeHigh / 60);
        }
        else if (nEstimateTime < 24*60*60)
        {
            text = tr("%1 - %2 Hours").arg(nRangeLow / (60*60)).arg(nRangeHigh / (60*60));
        }
        else
        {
            text = tr("%1 - %2 Days").arg(nRangeLow / (60*60*24)).arg(nRangeHigh / (60*60*24));
        }

        labelStakingIcon->setPixmap(QIcon(":/icons/staking_on").pixmap(28,54));
        labelStakingIcon->setEnabled(true);
		labelStakingIcon->setToolTip(tr("No issues detected:<br> <b>You are staking!</b><br><br>WYTF Network Weight: <b>%1</b><br><br>My Wallet Weight: <b>%2</b><br><br>Estimated Next Stake: <b>%4</b><br><br>MultiSend: <b>%5</b>").arg(nNetworkWeight).arg(nWeight).arg(text).arg(fMultiSend ? tr("Active"):tr("Inactive")));
		statisticsPage->setStrength(GetStrength(nWeight));
    }
    else
    {
        labelStakingIcon->setToolTip(tr("You are not staking!<br><br>WYTF Network Weight: <b>%1</b>").arg(nNetworkWeight));
        labelStakingIcon->setEnabled(false);
        labelStakingIcon->setPixmap(QIcon(":/icons/staking_off").pixmap(28,54));
    }
	
}

void BitcoinGUI::gotoStatisticsPage()
{
    statisticsAction->setChecked(true);
    centralWidget->setCurrentWidget(statisticsPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::error(const QString &title, const QString &message, bool modal)
{
    // Report errors from network/worker thread
    if(modal)
    {
        QMessageBox::critical(this, title, message, QMessageBox::Ok, QMessageBox::Ok);
    } else {
        notificator->notify(Notificator::Critical, title, message);
    }
}

void BitcoinGUI::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
#ifndef Q_OS_MAC // Ignored on Mac
    if(e->type() == QEvent::WindowStateChange)
    {
        if(clientModel && clientModel->getOptionsModel()->getMinimizeToTray())
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

void BitcoinGUI::closeEvent(QCloseEvent *event)
{
    if(clientModel)
    {
#ifndef Q_OS_MAC // Ignored on Mac
        if(!clientModel->getOptionsModel()->getMinimizeToTray() &&
           !clientModel->getOptionsModel()->getMinimizeOnClose())
        {
            qApp->quit();
        }
#endif
    }
    QMainWindow::closeEvent(event);
}

void BitcoinGUI::askFee(qint64 nFeeRequired, bool *payFee)
{
    QString strMessage =
        tr("This transaction is over the size limit.  You can still send it for a fee of %1, "
          "which goes to the nodes that process your transaction and helps to support the network.  "
          "Do you want to pay the fee?").arg(
                BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, nFeeRequired));
    QMessageBox::StandardButton retval = QMessageBox::question(
          this, tr("Confirm transaction fee"), strMessage,
          QMessageBox::Yes|QMessageBox::Cancel, QMessageBox::Yes);
    *payFee = (retval == QMessageBox::Yes);
}

void BitcoinGUI::incomingTransaction(const QModelIndex & parent, int start, int end)
{
    if(!walletModel || !clientModel)
        return;
    TransactionTableModel *ttm = walletModel->getTransactionTableModel();
    qint64 amount = ttm->index(start, TransactionTableModel::Amount, parent)
                    .data(Qt::EditRole).toULongLong();
    if(!clientModel->inInitialBlockDownload())
    {
		fMultiSendNotify = pwalletMain->fMultiSendNotify;
		
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
                            (amount)<0 ? (fMultiSendNotify == true ? tr("Sent MultiSend transaction") : tr("Sent transaction") ):
                                         tr("Incoming transaction"),
                              tr("Date: %1\n"
                                 "Amount: %2\n"
                                 "Type: %3\n"
                                 "Address: %4\n")
                              .arg(date)
                              .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), amount, true))
                              .arg(type)
                              .arg(address), icon);
							  
							  pwalletMain->fMultiSendNotify = false;
    }
}

void BitcoinGUI::checkWallet()
{

    int nMismatchSpent;
    int64_t nBalanceInQuestion;
    int nOrphansFound;

    if(!walletModel)
        return;

    // Check the wallet as requested by user
    walletModel->checkWallet(nMismatchSpent, nBalanceInQuestion, nOrphansFound);

    if (nMismatchSpent == 0 && nOrphansFound == 0)
        QString strMessage =
		tr("Check Wallet Information"),
                tr("Wallet passed integrity test!\n"
                   "Nothing found to fix.");
    else
		notificator->notify(Notificator::Warning, //tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid HyperStake address or malformed URI parameters."));
			tr("Check Wallet Information"), tr("Wallet failed integrity test!\n\n"
                  "Mismatched coin(s) found: %1.\n"
                  "Amount in question: %2.\n"
                  "Orphans found: %3.\n\n"
                  "Please backup wallet and run repair wallet.\n")
						.arg(nMismatchSpent)
                        .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), nBalanceInQuestion,true))
                        .arg(nOrphansFound));
}

void BitcoinGUI::repairWallet()
{
    int nMismatchSpent;
    int64_t nBalanceInQuestion;
    int nOrphansFound;

    if(!walletModel)
        return;

    // Repair the wallet as requested by user
    walletModel->repairWallet(nMismatchSpent, nBalanceInQuestion, nOrphansFound);

    if (nMismatchSpent == 0 && nOrphansFound == 0)
       notificator->notify(Notificator::Warning,
	   tr("Repair Wallet Information"),
               tr("Wallet passed integrity test!\n"
                  "Nothing found to fix."));
    else
		notificator->notify(Notificator::Warning,
		tr("Repair Wallet Information"),
               tr("Wallet failed integrity test and has been repaired!\n"
                  "Mismatched coin(s) found: %1\n"
                  "Amount affected by repair: %2\n"
                  "Orphans removed: %3\n")
                        .arg(nMismatchSpent)
                        .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), nBalanceInQuestion,true))
                        .arg(nOrphansFound));
}

void BitcoinGUI::gotoOverviewPage()
{
    overviewAction->setChecked(true);
    centralWidget->setCurrentWidget(overviewPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoHistoryPage()
{
    historyAction->setChecked(true);
    centralWidget->setCurrentWidget(transactionsPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), transactionView, SLOT(exportClicked()));
}

void BitcoinGUI::gotoAddressBookPage()
{
    addressBookAction->setChecked(true);
    centralWidget->setCurrentWidget(addressBookPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), addressBookPage, SLOT(exportClicked()));
}

void BitcoinGUI::gotoBlockBrowser(QString transactionId)
{
    if(!transactionId.isEmpty())
    blockBrowser->setTransactionId(transactionId);
    
    blockBrowser->show();
}

void BitcoinGUI::gotoReceiveCoinsPage()
{
    receiveCoinsAction->setChecked(true);
    centralWidget->setCurrentWidget(receiveCoinsPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), receiveCoinsPage, SLOT(exportClicked()));
}

void BitcoinGUI::gotoSendCoinsPage()
{
    sendCoinsAction->setChecked(true);
    centralWidget->setCurrentWidget(sendCoinsPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoSignMessageTab(QString addr)
{
    // call show() in showTab_SM()
    signVerifyMessageDialog->showTab_SM(true);

    if(!addr.isEmpty())
        signVerifyMessageDialog->setAddress_SM(addr);
}

void BitcoinGUI::gotoVerifyMessageTab(QString addr)
{
    // call show() in showTab_VM()
    signVerifyMessageDialog->showTab_VM(true);

    if(!addr.isEmpty())
        signVerifyMessageDialog->setAddress_VM(addr);
}

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
        int nValidUrisFound = 0;
        QList<QUrl> uris = event->mimeData()->urls();
        foreach(const QUrl &uri, uris)
        {
            if (sendCoinsPage->handleURI(uri.toString()))
                nValidUrisFound++;
        }

        // if valid URIs were found
        if (nValidUrisFound)
            gotoSendCoinsPage();
        else
            notificator->notify(Notificator::Warning, tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid WYTF address or malformed URI parameters."));
    }

    event->acceptProposedAction();
}

void BitcoinGUI::handleURI(QString strURI)
{
    // URI has to be valid
    if (sendCoinsPage->handleURI(strURI))
    {
        showNormalIfMinimized();
        gotoSendCoinsPage();
    }
    else
        notificator->notify(Notificator::Warning, tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid WYTF address or malformed URI parameters."));
}

void BitcoinGUI::setEncryptionStatus(int status)
{
    switch(status)
    {
    case WalletModel::Unencrypted:
        labelEncryptionIcon->show();
		labelEncryptionIcon->setPixmap(QIcon(":/icons/not_encrypted").pixmap(28,54));
        labelEncryptionIcon->setToolTip(tr("Your wallet is <b>not encrypted</b> and currently <b>unlocked</b>!<br><br>It is highly recommended to encrypt your wallet for safety purposes."));
        encryptWalletAction->setChecked(false);
        changePassphraseAction->setEnabled(false);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(false);
        encryptWalletAction->setEnabled(true);
        break;
    case WalletModel::Unlocked:
        labelEncryptionIcon->show();
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_open_toolbar").pixmap(28,54));
        labelEncryptionIcon->setToolTip(tr("Your wallet is <b>encrypted</b> and currently <b>unlocked</b>."));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    case WalletModel::Locked:
        labelEncryptionIcon->show();
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_closed_toolbar").pixmap(28,54));
        labelEncryptionIcon->setToolTip(tr("Your wallet is <b>encrypted</b> and currently <b>locked</b>"));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(true);
        lockWalletAction->setVisible(false);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    }
}

void BitcoinGUI::encryptWallet(bool status)
{
    if(!walletModel)
        return;
    AskPassphraseDialog dlg(status ? AskPassphraseDialog::Encrypt:
                                     AskPassphraseDialog::Decrypt, this);
    dlg.setModel(walletModel);
    dlg.exec();

    setEncryptionStatus(walletModel->getEncryptionStatus());
}

void BitcoinGUI::backupWallet()
{
    QString saveDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
    QString filename = QFileDialog::getSaveFileName(this, tr("Backup Wallet"), saveDir, tr("Wallet Data (*.dat)"));
    if(!filename.isEmpty()) {
        if(!walletModel->backupWallet(filename)) {
            QMessageBox::warning(this, tr("Backup Failed"), tr("There was an error trying to save the wallet data to the new location."));
        }
    }
}

void BitcoinGUI::changePassphrase()
{
    AskPassphraseDialog dlg(AskPassphraseDialog::ChangePass, this);
    dlg.setModel(walletModel);
    dlg.exec();
}

void BitcoinGUI::unlockWallet()
{
    if(!walletModel)
        return;
    // Unlock wallet when requested by wallet model
    if(walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog::Mode mode = sender() == unlockWalletAction ?
              AskPassphraseDialog::UnlockStaking : AskPassphraseDialog::Unlock;
        AskPassphraseDialog dlg(mode, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
}

void BitcoinGUI::lockWallet()
{
    if(!walletModel)
        return;

    walletModel->setWalletLocked(true);
}

void BitcoinGUI::showNormalIfMinimized(bool fToggleHidden)
{
    // activateWindow() (sometimes) helps with keyboard focus on Windows
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

void BitcoinGUI::updateStyleSlot()
{
    updateStyle();
}

void BitcoinGUI::updateStyle()
{
    if (!fUseWYTFTheme)
        return;

    QString qssPath = QString::fromStdString( GetDataDir().string() ) + "/WYTF_v1.0.0.2.qss";

    QFile f( qssPath );

    if (!f.exists())
        writeDefaultStyleSheet( qssPath );

    if (!f.open(QFile::ReadOnly))
    {
        qDebug() << "failed to open style sheet";
        return;
    }

    qDebug() << "loading theme";
    qApp->setStyleSheet( f.readAll() );
}

void BitcoinGUI::writeDefaultStyleSheet(const QString &qssPath)
{
    qDebug() << "writing default style sheet";

    QFile qss( ":/text/stylesheet" );
    qss.open( QFile::ReadOnly );

    QFile f( qssPath );
    f.open( QFile::ReadWrite );
    f.write( qss.readAll() );
}