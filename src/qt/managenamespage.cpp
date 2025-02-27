#include <qt/managenamespage.h>
#include <qt/forms/ui_managenamespage.h>

#include <qt/csvmodelwriter.h>
#include <qt/guiutil.h>
#include <qt/nametablemodel.h>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>

#include <QMessageBox>
#include <QMenu>
#include <QSortFilterProxyModel>

ManageNamesPage::ManageNamesPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    platformStyle(platformStyle),
    ui(new Ui::ManageNamesPage),
    model(nullptr),
    walletModel(nullptr),
    proxyModel(nullptr)
{
    ui->setupUi(this);

    // Context menu actions
    copyNameAction = new QAction(tr("Copy &Name"), this);
    copyValueAction = new QAction(tr("Copy &Value"), this);
    renewNameAction = new QAction(tr("&Renew Names"), this);

    // Build context menu
    contextMenu = new QMenu();
    contextMenu->addAction(copyNameAction);
    contextMenu->addAction(copyValueAction);
    contextMenu->addAction(renewNameAction);

    // Connect signals for context menu actions
    connect(copyNameAction, &QAction::triggered, this, &ManageNamesPage::onCopyNameAction);
    connect(copyValueAction, &QAction::triggered, this, &ManageNamesPage::onCopyValueAction);
    connect(renewNameAction, &QAction::triggered, this, &ManageNamesPage::onRenewNameAction);

    connect(ui->renewNameButton, &QPushButton::clicked, this, &ManageNamesPage::onRenewNameAction);

    connect(ui->tableView, &QTableView::customContextMenuRequested, this, &ManageNamesPage::contextualMenu);
    ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    ui->tableView->installEventFilter(this);
}

ManageNamesPage::~ManageNamesPage()
{
    delete ui;
}

void ManageNamesPage::setModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
    model = walletModel->getNameTableModel();

    proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(model);
    proxyModel->setDynamicSortFilter(true);
    proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

    ui->tableView->setModel(proxyModel);
    ui->tableView->sortByColumn(0, Qt::AscendingOrder);

    ui->tableView->horizontalHeader()->setHighlightSections(false);

    // Set column widths
    ui->tableView->horizontalHeader()->resizeSection(
            NameTableModel::Name, 320);
    ui->tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    connect(ui->tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &ManageNamesPage::selectionChanged);

    selectionChanged();
}

bool ManageNamesPage::eventFilter(QObject *object, QEvent *event)
{
    return QWidget::eventFilter(object, event);
}

void ManageNamesPage::selectionChanged()
{
    // Enable/disable UI elements based on number of names selected.
    QTableView *table = ui->tableView;
    if (!table->selectionModel())
        return;

    QModelIndexList indexes = GUIUtil::getEntryData(ui->tableView, NameTableModel::Name);

    const bool singleNameSelected = indexes.size() == 1;
    const bool anyNamesSelected = indexes.size() >= 1;

    // Context menu
    copyNameAction->setEnabled(singleNameSelected);
    copyValueAction->setEnabled(singleNameSelected);
    renewNameAction->setEnabled(anyNamesSelected);

    // Buttons
    ui->renewNameButton->setEnabled(anyNamesSelected);
}

void ManageNamesPage::contextualMenu(const QPoint &point)
{
    QModelIndex index = ui->tableView->indexAt(point);
    if (index.isValid())
        contextMenu->exec(QCursor::pos());
}

void ManageNamesPage::onCopyNameAction()
{
    GUIUtil::copyEntryData(ui->tableView, NameTableModel::Name);
}

void ManageNamesPage::onCopyValueAction()
{
    GUIUtil::copyEntryData(ui->tableView, NameTableModel::Value);
}

void ManageNamesPage::onRenewNameAction()
{
    QModelIndexList indexes = GUIUtil::getEntryData(ui->tableView, NameTableModel::Name);

    if (indexes.isEmpty())
        return;

    QString msg;
    QString title;

    if (indexes.size() == 1)
    {
        const QString &name = indexes.at(0).data(Qt::EditRole).toString();

        msg = tr ("Are you sure you want to renew the name <b>%1</b>?")
            .arg (GUIUtil::HtmlEscape (name));
        title = tr ("Confirm name renewal");
    }
    else
    {
        msg = tr ("Are you sure you want to renew multiple names simultaneously?  This will reveal common ownership of the renewed names (bad for anonymity).");
        title = tr ("Confirm multiple name renewal");
    }

    QMessageBox::StandardButton res;
    res = QMessageBox::question (this, title, msg,
                                 QMessageBox::Yes | QMessageBox::Cancel,
                                 QMessageBox::Cancel);
    if (res != QMessageBox::Yes)
        return;

    WalletModel::UnlockContext ctx(walletModel->requestUnlock ());
    if (!ctx.isValid ())
        return;

    for (const QModelIndex& index : indexes)
    {
        const QString &name = index.data(Qt::EditRole).toString();

        const QString err_msg = model->renew(name);
        if (!err_msg.isEmpty() && err_msg != "ABORTED")
        {
            QMessageBox::critical(this, tr("Name renew error"), err_msg);
            return;
        }
    }
}

void ManageNamesPage::exportClicked()
{
    // CSV is currently the only supported format
    QString suffixOut = "";
    QString filename = GUIUtil::getSaveFileName(
            this,
            tr("Export Registered Names Data"),
            QString(),
            tr("Comma separated file (*.csv)"),
            &suffixOut);

    if (filename.isNull())
        return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(proxyModel);
    writer.addColumn("Name", NameTableModel::Name, Qt::EditRole);
    writer.addColumn("Value", NameTableModel::Value, Qt::EditRole);
    writer.addColumn("Expires In", NameTableModel::ExpiresIn, Qt::EditRole);
    writer.addColumn("Name Status", NameTableModel::NameStatus, Qt::EditRole);

    if (!writer.write())
    {
        QMessageBox::critical(this, tr("Error exporting"), tr("Could not write to file %1.").arg(filename),
                              QMessageBox::Abort, QMessageBox::Abort);
    }
}
