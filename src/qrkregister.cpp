/*
 * This file is part of QRK - Qt Registrier Kasse
 *
 * Copyright (C) 2015-2019 Christian Kvasny <chris@ckvsoft.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Button Design, and Idea for the Layout are lean out from LillePOS, Copyright 2010, Martin Koller, kollix@aon.at
 *
*/

#include "database.h"
#include "qrkregister.h"
#include "qrktimedmessagebox.h"
#include "givendialog.h"
#include "qrkdelegate.h"
#include "r2bdialog.h"
#include "documentprinter.h"
#include "utils/utils.h"
#include "reports.h"
#include "pluginmanager/pluginmanager.h"
#include "qrkprogress.h"
#include "3rdparty/ckvsoft/rbac/acl.h"
#include "3rdparty/ckvsoft/headerview.h"
#include "3rdparty/ckvsoft/drag/dragflowwidget.h"
#include "3rdparty/ckvsoft/buttoncolumndelegate.h"
#include "barcodefinder.h"
#include "qrkmultimedia.h"
#include "3rdparty/ckvsoft/texteditdialog.h"
#include "ui_qrkregister.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QMessageBox>
#include <QJsonObject>
#include <QJsonArray>
#include <QDesktopWidget>
#include <QToolButton>
#include <QTimer>
#include <QKeyEvent>
#include <QShortcut>
#include <QTextDocument>
#include <QDir>
#include <QtWidgets>
#include <QMouseEvent>

QRKRegister::QRKRegister(QWidget *parent)
    : QWidget(parent), ui(new Ui::QRKRegister), m_currentReceipt(0)
{

    ui->setupUi(this);
    ui->numericKeyPad->setHidden(true);

    if ( QApplication::desktop()->width() < 1200 ) {
        ui->cancelRegisterButton->setMinimumWidth(0);
        ui->cashReceipt->setMinimumWidth(0);
        ui->creditcardReceipt->setMinimumWidth(0);
        ui->debitcardReceipt->setMinimumWidth(0);
        ui->plusButton->setMinimumWidth(0);
        ui->minusButton->setMinimumWidth(0);
        ui->receiptToInvoice->setMinimumWidth(0);
        ui->checkReceiptButton->setMinimumWidth(0);
        ui->receiptToInvoice->setMinimumWidth(0);
        ui->categoriePushButton->setMinimumWidth(0);
    }

    ui->checkReceiptButton->setToolTip(tr("Erstellt einen NULL (Kontroll) Beleg für den Prüfer vom BMF"));

    ui->safetyDeviceLabel->setVisible(false);

    connect(ui->receiptToInvoice, &QPushButton::clicked, this, &QRKRegister::receiptToInvoiceSlot);

    connect(ui->plusButton, &QPushButton::clicked, this, &QRKRegister::plusSlot);
    connect(ui->minusButton, &QPushButton::clicked, this, &QRKRegister::minusSlot);
    connect(ui->cancelRegisterButton, &QPushButton::clicked, this, &QRKRegister::onCancelRegisterButton_clicked);
    connect(ui->checkReceiptButton, &QPushButton::clicked, this, &QRKRegister::createCheckReceipt);

    QShortcut *plusButtonShortcut = new QShortcut(QKeySequence("Insert"), this);
    QShortcut *minusButtonShortcut = new QShortcut(QKeySequence("Delete"), this);

    QShortcut *plusKeyShortcut = new QShortcut(QKeySequence("+"), this);
    QShortcut *minusKeyShortcut = new QShortcut(QKeySequence("-"), this);

    connect(plusButtonShortcut, &QShortcut::activated, this, &QRKRegister::plusSlot);
    connect(minusButtonShortcut, &QShortcut::activated, this, &QRKRegister::minusSlot);
    connect(plusKeyShortcut, &QShortcut::activated, this, &QRKRegister::plusSlot);
    connect(minusKeyShortcut, &QShortcut::activated, this, &QRKRegister::minusSlot);

    ui->buttonGroup->setId(ui->cashReceipt, PAYED_BY_CASH);
    ui->buttonGroup->setId(ui->creditcardReceipt, PAYED_BY_CREDITCARD);
    ui->buttonGroup->setId(ui->debitcardReceipt, PAYED_BY_DEBITCARD);

    connect(ui->buttonGroup, static_cast<void(QButtonGroup::*)(int)>(&QButtonGroup::buttonClicked), this, &QRKRegister::onButtonGroup_payNow_clicked);

    m_orderListModel = new ReceiptItemModel(this);
    connect(m_orderListModel, &ReceiptItemModel::futureTimeDedected,this, &QRKRegister::futureTimeDedected);

    connect(ui->barcodeLineEdit, &QLineEdit::editingFinished, this, &QRKRegister::barcodeChangedSlot);

    connect(m_orderListModel, &ReceiptItemModel::setButtonGroupEnabled, this, &QRKRegister::setButtonGroupEnabled);
    connect(m_orderListModel, &ReceiptItemModel::finishedItemChanged, this, &QRKRegister::finishedItemChanged);
    connect(m_orderListModel, &ReceiptItemModel::finishedPlus, this, &QRKRegister::finishedPlus);
    connect(m_orderListModel, &ReceiptItemModel::singlePriceChanged, this, &QRKRegister::singlePriceChanged, Qt::DirectConnection);
    connect(m_orderListModel, &ReceiptItemModel::impossibleTotalPrice, this, &QRKRegister::impossibleTotalPrice, Qt::DirectConnection);

    ui->splitter->setCollapsible(0, false);
    ui->splitter->setSizes(QList<int>({0, 0}));

    connect(ui->splitter, &QSplitter::splitterMoved, this, &QRKRegister::splitterMoved);
    connect(ui->numericKeyPad, &NumericKeypad::valueButtonPressed, this, &QRKRegister::numPadValueButtonPressed);
    connect(ui->numericKeyPad, &NumericKeypad::textChanged, [=]() {
        ui->numPadLabel->setText(ui->numericKeyPad->text());
    });
    connect(ui->numPadpushButton, &QPushButton::clicked, this, &QRKRegister::numPadToogle);
    connect(ui->BarcodeFinderPushButton, &QPushButton::clicked, this, &QRKRegister::barcodeFinderButton_clicked);

    ui->orderList->setHorizontalHeader(new HeaderView(this));

    connect(ui->orderList->horizontalHeader(), &QHeaderView::sectionMoved, this, &QRKRegister::writeHeaderColumnSettings);
    connect(ui->orderList, &QTableView::clicked, this, &QRKRegister::columnClicked);

    connect(ui->upPushButton, &QPushButton::clicked, ui->QuickButtons, &QrkQuickButtons::upPushButton);
    connect(ui->downPushButton, &QPushButton::clicked, ui->QuickButtons, &QrkQuickButtons::downPushButton);
    connect(ui->QuickButtons, &QrkQuickButtons::addProductToOrderList, this, &QRKRegister::addProductToOrderList, Qt::QueuedConnection);
    connect(ui->categoriePushButton, &QPushButton::clicked, ui->QuickButtons, &QrkQuickButtons::backToMiddleButton);
    connect(ui->QuickButtons, &QrkQuickButtons::showCategoriePushButton, ui->categoriePushButton, &QPushButton::setVisible);
    connect(ui->QuickButtons, &QrkQuickButtons::enableCategoriePushButton, ui->categoriePushButton, &QPushButton::setEnabled);

    ui->orderList->installEventFilter(this);

    readSettings();

}

QRKRegister::~QRKRegister()
{
    writeSettings();
    delete ui;
    delete m_orderListModel;
}

void QRKRegister::keyPressEvent(QKeyEvent *event)
{
    if(event->key() == m_barcodeReaderPrefix) {
        ui->barcodeLineEdit->setFocus();
        ui->barcodeLineEdit->clear();
    }

    /*
    bool modifier = event->modifiers() == Qt::KeypadModifier;
    if(modifier)
        ui->numPadLabel->setFocus();
    */
    // qInfo() << "KeyPress: " << event->text();

}

bool QRKRegister::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);

        if (keyEvent->key() == Qt::Key_Return && m_barcodeInputLineEditDefault) {
            QTableView *v = static_cast<QTableView *>(obj);
            bool canSetFocus = true;
            if (v) {
                int col = v->currentIndex().column();
                canSetFocus = (col != REGISTER_COL_SINGLE) && (col != REGISTER_COL_TOTAL);
            }
           if (canSetFocus)
               ui->barcodeLineEdit->setFocus();
        }

        if(keyEvent->key() == m_barcodeReaderPrefix) {
            ui->barcodeLineEdit->setFocus();
            ui->barcodeLineEdit->clear();
            return true;
        }
    }

    return QObject::eventFilter(obj, event);
}

//--------------------------------------------------------------------------------

void QRKRegister::finishedItemChanged()
{
    ui->orderList->resizeColumnsToContents();
    ui->orderList->horizontalHeader()->setSectionResizeMode(REGISTER_COL_PRODUCT, QHeaderView::Stretch);
    ui->orderList->resizeRowsToContents();
    updateOrderSum();
    emit sendDatagram("orders", QJsonDocument(m_orderRoot).toJson(QJsonDocument::Compact));
}

//--------------------------------------------------------------------------------

void QRKRegister::init()
{
    initPlugins();

    QrkSettings settings;

    ui->barcodeLineEdit->setEnabled(true);
    ui->debitcardReceipt->setHidden(settings.value("hideDebitcardButton", false).toBool());
    ui->creditcardReceipt->setHidden(settings.value("hideCreditcardButton", false).toBool());
    ui->receiptToInvoice->setHidden(settings.value("hideR2BButton", false).toBool());

    ui->orderList->setModel(m_orderListModel);

    m_useDiscount = settings.value("useDiscount", false).toBool();
    m_orderListModel->setUseDiscount(m_useDiscount);
    ui->numericKeyPad->discountButtonSetEnabled(m_useDiscount);
    ui->numericKeyPad->setVisible(settings.value("virtualNumPad", false).toBool());

    m_useInputNetPrice = settings.value("useInputNetPrice", false).toBool();
    m_useMaximumItemSold = settings.value("useMaximumItemSold", false).toBool();
    m_useDecimalQuantity = settings.value("useDecimalQuantity", false).toBool();
    m_usePriceChangedDialog = settings.value("usePriceChangedDialog", true).toBool();
    m_useGivenDialog = settings.value("useGivenDialog", false).toBool();
    m_decimaldigits = settings.value("decimalDigits", 2).toInt();
    m_receiptPrintDialog = settings.value("useReceiptPrintedDialog", true).toBool();
    m_minstockDialog = settings.value("useMinstockDialog", false).toBool();
    m_useInputProductNumber = settings.value("useInputProductNumber", false).toBool();
    m_registerHeaderMoveable = settings.value("saveRegisterHeaderGeo", false).toBool();
    m_optionalDescription = settings.value("optionalDescription", false).toBool();
    ui->orderList->horizontalHeader()->setSectionsMovable(m_registerHeaderMoveable);

    settings.beginGroup("BarcodeReader");
    m_barcodeReaderPrefix = settings.value("barcodeReaderPrefix", Qt::Key_F11).toInt();
    m_barcodeInputLineEditDefault = settings.value("barcode_input_default", false).toBool();
    settings.endGroup();


    if (Database::getTaxLocation() == "AT")
        ui->checkReceiptButton->setVisible(true);
    else
        ui->checkReceiptButton->setVisible(false);

    readSettings();
    emit ui->QuickButtons->refresh();

}

void QRKRegister::safetyDevice(bool active)
{
    if (active) {
        ui->orderList->setStyleSheet("");
        ui->safetyDeviceLabel->setVisible(false);
    } else {
        ui->orderList->setStyleSheet("QTableView{border : 3px solid red}");
        ui->safetyDeviceLabel->setVisible(true);
    }
}


void QRKRegister::numPadValueButtonPressed(const QString &text, REGISTER_COL column)
{
    QModelIndex idx = ui->orderList->selectionModel()->currentIndex();
    if (idx.isValid()) {
        QBCMath newText(text);
        if (column == REGISTER_COL_COUNT)
            newText.round(m_decimaldigits);
        else
            newText.round(2);

        QStandardItem *item = m_orderListModel->item(idx.row(), column);
        if (item) {
            item->setText( newText.toString() );
            ui->orderList->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
            ui->orderList->edit(idx);
        }
    }
}

void QRKRegister::numPadToogle(bool)
{
    bool hidden = ui->numericKeyPad->isHidden();
    int h = QApplication::desktop()->height();

    if (hidden) {
        if (h <= 600)
            emit fullScreen(true);
        ui->numericKeyPad->setVisible(hidden);
    } else {
        if (h <= 600)
            emit fullScreen(false);
        ui->numericKeyPad->setVisible(hidden);
        emit ui->numericKeyPad->clear();
    }
}

void QRKRegister::barcodeFinderButton_clicked(bool)
{
    BarcodeFinder finder(this);
    connect(&finder, &BarcodeFinder::barcodeCommit, ui->barcodeLineEdit, &QLineEdit::setText);
    connect(&finder, &BarcodeFinder::barcodeCommit, ui->barcodeLineEdit, &QLineEdit::editingFinished);

    finder.exec();
    disconnect(&finder, &BarcodeFinder::barcodeCommit, Q_NULLPTR, Q_NULLPTR);

}

void QRKRegister::addProductToOrderList(int id)
{
    setFocus();
    bool forceOverwrite = false;
    int rc = m_orderListModel->rowCount();
    if (rc == 0) {
        plusSlot();
        rc = m_orderListModel->rowCount();
        forceOverwrite = true;
    }

    QSqlDatabase dbc = Database::database();
    QSqlQuery query(dbc);

    query.prepare(QString("SELECT name, tax, gross, itemnum FROM products WHERE id=%1").arg(id));
    bool ok = query.exec();

    if (!ok) {
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Error: " << query.lastError().text();
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);
        return;
    }

    if (query.next()) {
        QString name = query.value("name").toString();
        QList<QStandardItem*> list = m_orderListModel->findItems(name, Qt::MatchExactly,REGISTER_COL_PRODUCT);
        if (list.count() > 0 && !forceOverwrite) {
            foreach( QStandardItem *item, list ) {
                int row = item->row();
                QBCMath count(m_orderListModel->item(row, REGISTER_COL_COUNT)->text().toDouble());
                if (QLocale().toDouble(ui->numPadLabel->text()) > 0.00)
                    count += QLocale().toDouble(ui->numPadLabel->text());
                else
                    count += 1;
                count.round(m_decimaldigits);
                m_orderListModel->item(row, REGISTER_COL_COUNT)->setText( count.toString() );
                //                QModelIndex idx = m_orderListModel->index(row, REGISTER_COL_COUNT);
                QModelIndex idx = m_orderListModel->index(row, ui->orderList->horizontalHeader()->logicalIndex(0));

                if (!m_barcodeInputLineEditDefault) {
                    ui->orderList->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
                    ui->orderList->edit(idx);
                } else {
                    ui->barcodeLineEdit->setFocus();
                }
                ui->numericKeyPad->clear();

                return;
            }
        }

        bool newItem = m_orderListModel->item(rc -1, REGISTER_COL_PRODUCT)->text().isEmpty();
        if (!newItem && !forceOverwrite) {
            plusSlot();
            rc = m_orderListModel->rowCount();
        }

        QBCMath count(1.00);
        if (QLocale().toDouble(ui->numPadLabel->text()) > 0.00)
            count = QLocale().toDouble(ui->numPadLabel->text());

        if (m_useDecimalQuantity)
            count.round(m_decimaldigits);
        else
            count = count.toInt();

        m_orderListModel->item(rc -1, REGISTER_COL_COUNT)->setText( count.toString() );
        m_orderListModel->item(rc -1, REGISTER_COL_PRODUCTNUMBER)->setText( query.value("itemnum").toString() );
        m_orderListModel->item(rc -1, REGISTER_COL_PRODUCT)->setText( query.value("name").toString() );
        m_orderListModel->item(rc -1, REGISTER_COL_TAX)->setText( query.value("tax").toString() );
        m_orderListModel->item(rc -1, REGISTER_COL_SINGLE)->setText( query.value("gross").toString() );

        if (!m_barcodeInputLineEditDefault) {
            //            QModelIndex idx = m_orderListModel->index(rc -1, REGISTER_COL_COUNT);
            QModelIndex idx = m_orderListModel->index(rc - 1, ui->orderList->horizontalHeader()->logicalIndex(0));
            ui->orderList->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
        } else {
            ui->barcodeLineEdit->setFocus();
        }
        ui->numericKeyPad->clear();

    }
}

//--------------------------------------------------------------------------------

void QRKRegister::updateOrderSum()
{
    double sum = 0;
    bool enabled = true;
    int rows = m_orderListModel->rowCount();
    if (rows == 0)
        setButtonGroupEnabled(false);

    QJsonObject Root;
    QJsonArray Orders;

    for (int row = 0; row < rows; row++) {
        QJsonObject order;

        QStringList dTemp = m_orderListModel->data(m_orderListModel->index(row, REGISTER_COL_TOTAL, QModelIndex())).toString().split(" ");
        QString sTemp = m_orderListModel->data(m_orderListModel->index(row, REGISTER_COL_PRODUCT, QModelIndex())).toString();
        if (sTemp.isEmpty()) {
            enabled = false;
        } else {
            order["product"] = sTemp;
            order["count"] = m_orderListModel->data(m_orderListModel->index(row, REGISTER_COL_COUNT, QModelIndex())).toDouble();
            order["sum"] = dTemp[0].toDouble();
            Orders.append(order);
        }
        double d4 = dTemp[0].toDouble();

        sum += d4;

    }

    Root["Orders"] = Orders;
    Root["sum"] = sum;
    m_orderRoot = Root;

    if (rows > 0)
        setButtonGroupEnabled(enabled);

    ui->sumLabel->setText(tr("%1 %2").arg(QLocale().toString(sum, 'f', 2)).arg(Database::getCurrency()));

}

//--------------------------------------------------------------------------------
bool QRKRegister::finishReceipts(int payedBy, int id, bool isReport)
{
    QRKProgress waitBar;

    waitBar.setText(tr("Beleg wird erstellt."));
    waitBar.setWaitMode();
    waitBar.show();

    qApp->processEvents();

    QDateTime dt = QDateTime::currentDateTime();;
    m_orderListModel->setCustomerText(ui->customerText->text());
    m_orderListModel->setReceiptTime(dt);
    bool ret = m_orderListModel->finishReceipts(payedBy, id, isReport);

    waitBar.close();

    return ret;
}

void QRKRegister::setButtonGroupEnabled(bool enabled)
{
    ui->cashReceipt->setEnabled(enabled);
    ui->creditcardReceipt->setEnabled(enabled);
    ui->debitcardReceipt->setEnabled(enabled);
}

//--------------------------------------------------------------------------------

void QRKRegister::newOrder()
{

    m_orderListModel->newOrder(false);

    QStringList list = Database::getLastReceipt();

    ui->customerText->clear();
    QSqlDatabase dbc = Database::database();
    QSqlQuery query(dbc);
    query.prepare("SELECT text FROM customer GROUP BY text");
    query.exec();

    QStringList completerList;

    while(query.next()){
        QString value = query.value("text").toString();
        completerList << value;
    }

    QCompleter *editorCompleter = new QCompleter(completerList) ;
    editorCompleter->setCaseSensitivity( Qt::CaseInsensitive ) ;
    editorCompleter->setFilterMode( Qt::MatchContains );
    ui->customerText->setCompleter( editorCompleter );

    ui->receiptToInvoice->setEnabled(true && RBAC::Instance()->hasPermission("register_r2b"));

    if (!list.empty()) {
        QString date = list.takeAt(0);
        QString localeDate = QLocale().toString(QDateTime::fromString(date, Qt::ISODate), QLocale::ShortFormat);

        QString bonNr = list.takeAt(0);
        QString taxName = Database::getActionType(list.takeAt(0).toInt());
        double gross = list.takeAt(0).toDouble();

        if (bonNr.length()) {
            QMap<int, double> givenMap = Database::getGiven(bonNr.toInt());
            if (givenMap.size() > 0) {
                QString payedByText;
                QBCMath given(givenMap.value(PAYED_BY_CASH));
                given.round(2);
                QBCMath secondPay(givenMap.value(PAYED_BY_DEBITCARD));
                if (secondPay == 0) secondPay = givenMap.value(PAYED_BY_CREDITCARD);
                secondPay.round(2);

                QBCMath retourMoney((given + secondPay) - gross);
                retourMoney.round(2);
                QString givenText = tr("BAR: %1 %2").arg(given.toLocale()).arg(Database::getCurrency());

                QString retourMoneyText = "";
                if (retourMoney > 0.0) {
                    retourMoneyText = tr("Rückgeld: %1 %2").arg(retourMoney.toLocale()).arg(Database::getCurrency());
                    if (secondPay == 0.0) givenText = tr("Gegeben: %1 %2").arg(given.toLocale()).arg(Database::getCurrency());
                }
                if (secondPay > 0.0) {
                    QString secondText = tr("%1: %2 %3").arg(givenMap.lastKey() == PAYED_BY_DEBITCARD?tr("Bankomat"):tr("Kreditkarte")).arg(secondPay.toLocale()).arg(Database::getCurrency());
                    payedByText = tr("Mischzahlung %1 %2\t%3 %4").arg(QLocale().toString(gross, 'f', 2)).arg(Database::getCurrency()).arg(givenText).arg(secondText);
                } else {
                    payedByText = tr("%1 %2 %3\t%4 %5").arg(taxName).arg(QLocale().toString(gross, 'f', 2)).arg(Database::getCurrency()).arg(givenText).arg(retourMoneyText);
                }
                ui->lastReceiptLabel->setText(tr("Letzter Barumsatz: BON Nr. %1 %2 %3").arg(bonNr).arg(localeDate).arg(payedByText));
            } else {
                ui->lastReceiptLabel->setText(QString(tr("Letzter Barumsatz: BON Nr. %1, %2, %3: %4 %5"))
                                              .arg(bonNr)
                                              .arg(localeDate)
                                              .arg(taxName)
                                              .arg(QLocale().toString(gross, 'f', 2)).arg(Database::getCurrency()));
            }
        }
    }

    ui->orderList->setAutoScroll(true);
    ui->orderList->setWordWrap(true);
    ui->orderList->setSelectionMode(QAbstractItemView::MultiSelection);
    ui->orderList->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);

    if (m_useDecimalQuantity)
        ui->orderList->setItemDelegateForColumn(REGISTER_COL_COUNT, new QrkDelegate (QrkDelegate::DOUBLE_SPINBOX, this));
    else
        ui->orderList->setItemDelegateForColumn(REGISTER_COL_COUNT, new QrkDelegate (QrkDelegate::SPINBOX, this));
    ui->orderList->setItemDelegateForColumn(REGISTER_COL_PRODUCT, new QrkDelegate (QrkDelegate::PRODUCTS, this));
    ui->orderList->setItemDelegateForColumn(REGISTER_COL_PRODUCTNUMBER, new QrkDelegate (QrkDelegate::PRODUCTSNUMBER, this));
    ui->orderList->setItemDelegateForColumn(REGISTER_COL_TAX, new QrkDelegate (QrkDelegate::COMBO_TAX, this));
    ui->orderList->setItemDelegateForColumn(REGISTER_COL_NET, new QrkDelegate (QrkDelegate::NUMBERFORMAT_DOUBLE, this));
    ui->orderList->setItemDelegateForColumn(REGISTER_COL_SINGLE, new QrkDelegate (QrkDelegate::NUMBERFORMAT_DOUBLE, this));
    ui->orderList->setItemDelegateForColumn(REGISTER_COL_DISCOUNT, new QrkDelegate (QrkDelegate::DISCOUNT, this));
    ui->orderList->setItemDelegateForColumn(REGISTER_COL_TOTAL, new QrkDelegate (QrkDelegate::NUMBERFORMAT_DOUBLE, this));
    ui->orderList->setItemDelegateForColumn(REGISTER_COL_DESCBUTTON, new ButtonColumnDelegate(":src/icons/textfield.png", this));

    readHeaderColumnSettings();

    ui->orderList->horizontalHeader()->setSectionResizeMode(REGISTER_COL_PRODUCT, QHeaderView::Stretch);
    ui->orderList->setColumnHidden(REGISTER_COL_NET, !m_useInputNetPrice);
    ui->orderList->setColumnHidden(REGISTER_COL_DISCOUNT, !m_useDiscount);
    ui->orderList->setColumnHidden(REGISTER_COL_PRODUCTNUMBER, !m_useInputProductNumber);
    ui->orderList->setColumnHidden(REGISTER_COL_DESCBUTTON, !m_optionalDescription);
    ui->orderList->setColumnHidden(REGISTER_COL_SAVE, true); /* TODO: Make usable and add code to Settings */

    ui->orderList->setColumnHidden(REGISTER_COL_TAX, m_orderlistTaxColumnHidden);
    ui->orderList->setColumnHidden(REGISTER_COL_SINGLE, m_orderlistSinglePriceColumnHidden);

    plusSlot();

}

void QRKRegister::createCheckReceipt(bool)
{

    QRKProgress waitBar;
    waitBar.setText(tr("Beleg wird erstellt."));
    waitBar.setWaitMode();
    waitBar.show();

    qApp->processEvents();

    Reports rep;
    bool ret = rep.checkEOAny();
    if (!ret) {
        return;
    }

    if (m_orderListModel->createNullReceipt(CONTROL_RECEIPT)) {
        emit finishedReceipt();

        if (m_receiptPrintDialog) {
            QrkTimedMessageBox messageBox(10,
                                          QMessageBox::Information,
                                          tr("Drucker"),
                                          tr("Kontrollbeleg wurde gedruckt. Nächster Vorgang wird gestartet."),
                                          QMessageBox::Yes | QMessageBox::Default
                                          );

            messageBox.setDefaultButton(QMessageBox::Yes);
            messageBox.setButtonText(QMessageBox::Yes, tr("OK"));
            messageBox.exec();
        }
    } else {
        qCritical() << "Function Name: " << Q_FUNC_INFO << " Error: can't create CheckReceipt";
    }
}

//-------------SLOTS--------------------------------------------------------------
void QRKRegister::barcodeChangedSlot()
{

    if (ui->barcodeLineEdit->text().isEmpty())
        return;

    QString barcode = ui->barcodeLineEdit->text();
    ui->barcodeLineEdit->clear();

    bool processed = false;
    if (barcodesInterface) {
        int index = ui->orderList->currentIndex().row();
        processed = barcodesInterface->process(m_orderListModel, index, barcode);
    }

    if (!processed) {
        int id = Database::getProductIdByBarcode(barcode);
        if (id > 0) {
            addProductToOrderList(id);
            QrkMultimedia::play(QRKMULTIMEDIA::BARCODE_SUCCSESS);
        } else {
            if (!m_barcodeInputLineEditDefault)
                setFocus();
            QrkMultimedia::play(QRKMULTIMEDIA::BARCODE_FAILURE);
        }
    }
}

//--------------------------------------------------------------------------------

void QRKRegister::plusSlot()
{

    m_isR2B = false;

    if (m_orderListModel->rowCount() > 0)
    {

        if (! ui->plusButton->isEnabled()) {
            ui->plusButton->setEnabled(true);
            return;
        }
    }

    m_orderListModel->plus();
}

void QRKRegister::finishedPlus()
{
    int row = m_orderListModel->rowCount() -1;

    if (m_useMaximumItemSold) {
        QStringList list;
        list = Database::getMaximumItemSold();
        m_orderListModel->setItem(row, REGISTER_COL_PRODUCT, new QStandardItem(list.at(0)));
        m_orderListModel->setItem(row, REGISTER_COL_TAX, new QStandardItem(list.at(1)));
        m_orderListModel->setItem(row, REGISTER_COL_SINGLE, new QStandardItem(list.at(2)));
    }

    ui->orderList->selectRow(row);
    ui->orderList->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->orderList->setSelectionBehavior(QAbstractItemView::SelectRows);

    /* TODO: Workaround ... resize set only once will not work
         * but here col REGISTER_COL_PRODUCT will lost QHeaderView::Stretch
         */
    ui->orderList->resizeColumnsToContents();
    ui->orderList->horizontalHeader()->setSectionResizeMode(REGISTER_COL_PRODUCT, QHeaderView::Stretch);

    if (m_orderListModel->rowCount() > 1)
        ui->receiptToInvoice->setEnabled(false);

    ui->plusButton->setEnabled(true);

    //    QModelIndex idx = m_orderListModel->index(row, REGISTER_COL_COUNT);
    QModelIndex idx = m_orderListModel->index(row, ui->orderList->horizontalHeader()->logicalIndex(0));

    if (!m_barcodeInputLineEditDefault) {
        ui->orderList->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
        ui->orderList->edit(idx);
    } else {
        ui->barcodeLineEdit->setFocus();
    }

    updateOrderSum();

}

//--------------------------------------------------------------------------------

void QRKRegister::minusSlot()
{

    m_isR2B = false;
    //get selections
    QItemSelection selection = ui->orderList->selectionModel()->selection();

    //find out selected rows
    QList<int> removeRows;
    foreach(QModelIndex index, selection.indexes()) {
        if(!removeRows.contains(index.row())) {
            removeRows.append(index.row());
        }
    }

    //loop through all selected rows
    for(int i=0;i<removeRows.count();++i)
    {
        //decrement all rows after the current - as the row-number will change if we remove the current
        for(int j=i;j<removeRows.count();++j) {
            if(removeRows.at(j) > removeRows.at(i)) {
                removeRows[j]--;
            }
        }
        //remove the selected row
        m_orderListModel->removeRow(removeRows.at(i));
    }

    if (m_orderListModel->rowCount() < 2)
        ui->receiptToInvoice->setEnabled(true && RBAC::Instance()->hasPermission("register_r2b"));

    if (m_orderListModel->rowCount() < 1)
        ui->plusButton->setEnabled(true);

    updateOrderSum();
    emit finishedItemChanged();
}

//--------------------------------------------------------------------------------

void QRKRegister::onButtonGroup_payNow_clicked(int payedBy)
{

    setButtonGroupEnabled(false);

    QDateTime dateTime;
    dateTime = QDateTime::currentDateTime();

    Reports rep;
    bool ret = rep.checkEOAny(dateTime);
    if (!ret) {
        setButtonGroupEnabled(true);
        return;
    }

    emit sendDatagram("topay", ui->sumLabel->text().replace(Database::getCurrency() ,""));

    if (m_useGivenDialog && payedBy == PAYED_BY_CASH) {
        double sum = QLocale().toDouble(ui->sumLabel->text().replace(Database::getCurrency() ,""));
        GivenDialog given(sum, this);
        if (given.exec() == 0) {
            qApp->processEvents();
            setButtonGroupEnabled(true);
            return;
        }
        m_orderListModel->setGiven(given.getGiven());
    }

    if (m_orderListModel->rowCount() > 0)
    {
        int rc = m_orderListModel->rowCount();

        for(int row = 0; row < rc; row++) {
            /* TODO: check for Autosave */
            bool checked = m_orderListModel->item(row ,REGISTER_COL_SAVE)->checkState();
            Q_UNUSED(checked);

            QJsonObject itemdata;
            itemdata["name"] = m_orderListModel->data(m_orderListModel->index(row, REGISTER_COL_PRODUCT, QModelIndex())).toString();
            itemdata["tax"] = m_orderListModel->data(m_orderListModel->index(row, REGISTER_COL_TAX, QModelIndex())).toDouble();
            itemdata["net"] = m_orderListModel->data(m_orderListModel->index(row, REGISTER_COL_NET, QModelIndex())).toDouble();
            itemdata["gross"] = m_orderListModel->data(m_orderListModel->index(row, REGISTER_COL_SINGLE, QModelIndex())).toDouble();
            itemdata["itemnum"] = m_orderListModel->data(m_orderListModel->index(row, REGISTER_COL_PRODUCTNUMBER, QModelIndex())).toString();
            itemdata["visible"] = 1;

            Database::addProduct(itemdata);
        }
    }

    QSqlDatabase dbc = Database::database();
    dbc.transaction();

    m_currentReceipt = m_orderListModel->createReceipts();
    bool sql_ok = true;
    if ( m_currentReceipt ) {
        if ( m_orderListModel->createOrder() ) {
            m_orderListModel->setR2B(m_isR2B);
            if ( finishReceipts(payedBy) ){
                emit finishedReceipt();
            } else {
                sql_ok = false;
            }
        } else {
            sql_ok = false;
        }
    }

    if (sql_ok) {
        dbc.commit();
        if (m_receiptPrintDialog) {
            QrkTimedMessageBox messageBox(10,
                                          QMessageBox::Information,
                                          tr("Drucker"),
                                          tr("Beleg %1 wurde gedruckt. Nächster Vorgang wird gestartet.").arg(m_currentReceipt),
                                          QMessageBox::Yes | QMessageBox::Default
                                          );

            messageBox.setDefaultButton(QMessageBox::Yes);
            messageBox.setButtonText(QMessageBox::Yes, QObject::tr("OK"));
            messageBox.exec();
        }

        QStringList stockList = Database::getStockInfoList();
        if (m_minstockDialog && stockList.count() > 0) {
            QMessageBox messageBox(QMessageBox::Information,
                                   tr("Lagerbestand"),
                                   tr("Mindestbestand wurde erreicht oder unterschritten!"),
                                   QMessageBox::Yes | QMessageBox::Default
                                   );

            messageBox.setDefaultButton(QMessageBox::Yes);
            messageBox.setButtonText(QMessageBox::Yes, tr("OK"));
            messageBox.setDetailedText(stockList.join('\n'));

            messageBox.exec();
        }
    } else {
        sql_ok = dbc.rollback();
        QMessageBox::warning(this, tr("Fehler"), tr("Datenbank und/oder Signatur Fehler!\nAktueller BON kann nicht erstellt werden. (Rollback: %1).\nÜberprüfen Sie ob genügend Speicherplatz für die Datenbank vorhanden ist. Weitere Hilfe gibt es im Forum. http:://www.ckvsoft.at").arg(sql_ok?tr("durchgeführt"):tr("fehlgeschlagen") ));
        qCritical() << "Function Name: " << Q_FUNC_INFO << " Error: " << dbc.lastError().text();
    }

    emit sendDatagram("finished", "");

}

//--------------------------------------------------------------------------------

void QRKRegister::receiptToInvoiceSlot()
{

    bool checkReceiptButtonSaved = ui->checkReceiptButton->isEnabled();
    bool receiptToInvoiceSaved = ui->receiptToInvoice->isEnabled();
    ui->checkReceiptButton->setEnabled(false);
    ui->receiptToInvoice->setEnabled(false);

    R2BDialog r2b(this);
    if ( r2b.exec() == QDialog::Accepted )
    {
        int rc = m_orderListModel->rowCount();
        if (rc == 0) {
            plusSlot();
            rc++;
        }

        bool productExists = Database::exists(r2b.getInvoiceNum());
        if (productExists) {
            QMessageBox msgWarning(QMessageBox::Warning,"","");
            msgWarning.setWindowTitle(QObject::tr("Warnung"));
            msgWarning.setText(QObject::tr("Die angegebene Rechnungsnummer wurde schon verwendet. Sollte es sich um einen Irrtum handeln dann Klicken Sie bitte nochmals den \"BON zu Rechnung #\" Button."));
            msgWarning.exec();
        }

        if (rc == 1) {
            m_isR2B = true;

            m_orderListModel->blockSignals(true);
            m_orderListModel->item(0, REGISTER_COL_COUNT)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_orderListModel->item(0, REGISTER_COL_PRODUCT)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_orderListModel->item(0, REGISTER_COL_NET)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_orderListModel->item(0, REGISTER_COL_TAX)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_orderListModel->item(0, REGISTER_COL_SINGLE)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_orderListModel->item(0, REGISTER_COL_DISCOUNT)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_orderListModel->item(0, REGISTER_COL_TOTAL)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

            m_orderListModel->item(0, REGISTER_COL_COUNT)->setText( "1" );
            m_orderListModel->item(0, REGISTER_COL_PRODUCT)->setText( r2b.getInvoiceNum() );
            m_orderListModel->item(0, REGISTER_COL_TAX)->setText( "0" );
            m_orderListModel->blockSignals(false);
            m_orderListModel->item(0, REGISTER_COL_SINGLE)->setText( r2b.getInvoiceSum() );

            ui->plusButton->setEnabled(false);

        }
    }
    ui->checkReceiptButton->setEnabled(checkReceiptButtonSaved);
    ui->receiptToInvoice->setEnabled(receiptToInvoiceSaved && RBAC::Instance()->hasPermission("register_r2b"));

}

//--------------------------------------------------------------------------------

void QRKRegister::clearModel()
{
    m_orderListModel->clear();
    m_orderListModel->setRowCount(0);
}

//--------------------------------------------------------------------------------

void QRKRegister::setCurrentReceiptNum(int id)
{
    m_currentReceipt = id;
}

//--------------------------------------------------------------------------------

void QRKRegister::onCancelRegisterButton_clicked()
{

    emit sendDatagram("cancelregister", "");
    emit ui->QuickButtons->backToMiddleButton(true);

    writeSettings();


    if (m_orderListModel->rowCount() > 0 ) {
        if (m_orderListModel->item(0, REGISTER_COL_PRODUCT)->text() == "") {
            emit cancelRegisterButton_clicked();
            return;
        }
    } else {
        emit cancelRegisterButton_clicked();
        return;
    }

    QMessageBox msgBox;
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText(tr("Kassenmodus verlassen? Ein nicht abgeschlossener Bon wird gelöscht!"));
    msgBox.setStandardButtons(QMessageBox::Yes);
    msgBox.addButton(QMessageBox::No);
    msgBox.setButtonText(QMessageBox::Yes, tr("Ja"));
    msgBox.setButtonText(QMessageBox::No, tr("Nein"));
    msgBox.setDefaultButton(QMessageBox::No);

    if(msgBox.exec() == QMessageBox::Yes){
        emit cancelRegisterButton_clicked();
    }
    ui->barcodeLineEdit->setEnabled(false);
}

void QRKRegister::setColumnHidden(int col)
{
    ui->orderList->setColumnHidden(col, true);
}

void QRKRegister::initPlugins()
{
    /*FIXME This is a workaround. I can't find the issue
     * After Calling PluginView this plugin will not work
     */

    /*
    if (barcodesInterface) {
        delete barcodesInterface;
        barcodesInterface = 0;
    }
*/
    if (!barcodesInterface) {
        barcodesInterface = qobject_cast<BarcodesInterface *>(PluginManager::instance()->getObjectByName("BarCodes"));
        if (barcodesInterface) {
            if (barcodesInterface->isActivated()) {
                connect(barcodesInterface, &BarcodesInterface::minusSlot, this, &QRKRegister::minusSlot, Qt::DirectConnection);
                connect(barcodesInterface, &BarcodesInterface::setColumnHidden, this, &QRKRegister::setColumnHidden, Qt::DirectConnection);
                connect(barcodesInterface, &BarcodesInterface::finishedReceipt, this, &QRKRegister::finishedReceipt, Qt::DirectConnection);
            } else {
                delete barcodesInterface;
                barcodesInterface = Q_NULLPTR;
            }
        }
    }    
}

void QRKRegister::writeSettings()
{
    QrkSettings settings;

    settings.beginGroup("Register");
    settings.save2Settings("splitterGeometry", ui->splitter->saveGeometry(), false);
    settings.save2Settings("splitterState", ui->splitter->saveState(), false);
    settings.save2Settings("TaxColumnHidden", m_orderlistTaxColumnHidden);
    settings.save2Settings("SinglePriceColumnHidden", m_orderlistSinglePriceColumnHidden);

    settings.endGroup();
}

void QRKRegister::writeHeaderColumnSettings(int, int, int)
{
    if (!m_registerHeaderMoveable)
        return;

    QrkSettings settings;

    settings.beginGroup("Register");
    settings.save2Settings("HorizontalHeaderColumnGeo", ui->orderList->horizontalHeader()->saveGeometry());
    settings.save2Settings("HorizontalHeaderColumnSta", ui->orderList->horizontalHeader()->saveState());

    settings.endGroup();
}

void QRKRegister::readSettings()
{
    QrkSettings settings;

    settings.beginGroup("Register");
    ui->splitter->restoreGeometry(settings.value("splitterGeometry").toByteArray());
    ui->splitter->restoreState(settings.value("splitterState").toByteArray());

    m_orderlistTaxColumnHidden = settings.value("TaxColumnHidden", false).toBool();
    m_orderlistSinglePriceColumnHidden = settings.value("SinglePriceColumnHidden", false).toBool();

    ui->orderList->horizontalHeader()->restoreGeometry(settings.value("HorizontalHeaderColumnGeo").toByteArray());
    ui->orderList->horizontalHeader()->restoreState(settings.value("HorizontalHeaderColumnSta").toByteArray());
    settings.endGroup();

    setButtonsHidden();
}

void QRKRegister::readHeaderColumnSettings()
{
    if (!m_registerHeaderMoveable)
        return;

    QrkSettings settings;

    settings.beginGroup("Register");
    ui->orderList->horizontalHeader()->restoreGeometry(settings.value("HorizontalHeaderColumnGeo").toByteArray());
    ui->orderList->horizontalHeader()->restoreState(settings.value("HorizontalHeaderColumnSta").toByteArray());

    settings.endGroup();
}

void QRKRegister::splitterMoved(int pos, int)
{
    if (pos < 600)
        m_orderlistTaxColumnHidden = true;
    else
        m_orderlistTaxColumnHidden = false;

    if (pos < 500)
        m_orderlistSinglePriceColumnHidden = true;
    else
        m_orderlistSinglePriceColumnHidden = false;

    ui->orderList->setColumnHidden(REGISTER_COL_TAX, m_orderlistTaxColumnHidden);
    ui->orderList->setColumnHidden(REGISTER_COL_SINGLE, m_orderlistSinglePriceColumnHidden);
    ui->orderList->resizeRowsToContents();
    setButtonsHidden();

}

void QRKRegister::setButtonsHidden()
{
    if (ui->splitter->sizes().at(1) == 0){
        ui->categoriePushButton->setHidden(true);
        ui->upPushButton->setHidden(true);
        ui->downPushButton->setHidden(true);
    } else {
        ui->categoriePushButton->setHidden(false);
        ui->upPushButton->setHidden(false);
        ui->downPushButton->setHidden(false);
    }
}

void QRKRegister::columnClicked(const QModelIndex &idx)
{
    if (idx.column() == REGISTER_COL_DESCBUTTON) {
        qDebug() << idx;
        QString data = m_orderListModel->data(idx, CUSTOMDATAROLE::CUSTOMDATA).toString();
        TextEditDialog ted(this);
        ted.setText(data);
        if (ted.exec() == QDialog::Accepted) {
            data = ted.getText();
            m_orderListModel->setData(idx, data, CUSTOMDATAROLE::CUSTOMDATA);
        }
        if (m_barcodeInputLineEditDefault)
            ui->barcodeLineEdit->setFocus();
    }
}

void QRKRegister::singlePriceChanged(QString product, QString singleprice, QString tax)
{
    if (!m_usePriceChangedDialog) {
        if (m_barcodeInputLineEditDefault)
            ui->barcodeLineEdit->setFocus();
        return;
    }

    if (!Database::exists(product)) {
        if (m_barcodeInputLineEditDefault)
            ui->barcodeLineEdit->setFocus();
        return;
    }

    QJsonObject data = Database::getProductByName(product);
    if (data.isEmpty()) {
        if (m_barcodeInputLineEditDefault)
            ui->barcodeLineEdit->setFocus();
        return;
    }

    QBCMath oldTax = data["tax"].toDouble();
    oldTax.round(2);
    QBCMath newTax(tax);
    newTax.round(2);
    if (newTax.toString().compare(oldTax.toString()) != 0) {
        if (m_barcodeInputLineEditDefault)
            ui->barcodeLineEdit->setFocus();
        return;
    }

    QBCMath oldGross = data["gross"].toDouble();
    oldGross.round(2);
    QBCMath newGross(singleprice);
    newGross.round(2);

    int diff = newGross.toString().compare(oldGross.toString());
    if ( diff != 0) {
        QrkTimedMessageBox messageBox(10,
                                      QMessageBox::Information,
                                      tr("Preisänderung"),
                                      tr("Soll der neue Preis ( %1 %2.- ) für den Artikel \"%3\" dauerhaft gespeichert werden?").arg(Database::getShortCurrency()).arg(newGross.toLocale()).arg(product),
                                      QMessageBox::Yes | QMessageBox::Default
                                      );

        messageBox.addButton(QMessageBox::No);
        messageBox.setDefaultButton(QMessageBox::No);
        messageBox.setButtonText(QMessageBox::Yes, tr("Ja"));
        messageBox.setButtonText(QMessageBox::No, tr("Nein"));

        if(messageBox.exec() == QMessageBox::Yes){
            Database::updateProductPrice(newGross.toDouble(), product);
        }
    }

    if (m_barcodeInputLineEditDefault)
        ui->barcodeLineEdit->setFocus();

}

void QRKRegister::impossibleTotalPrice(const QString &s, const QString &s2)
{
    QrkTimedMessageBox messageBox(10,
                                  QMessageBox::Information,
                                  tr("Preisänderung"),
                                  tr("Dieser Preis (%1 %2) musste leider gerundet werden (%3 %4) da der Einzelpreis aufgrund der angegebenen Steuer nicht anders rundbar ist.").arg(s).arg(Database::getShortCurrency()).arg(s2).arg(Database::getShortCurrency()),
                                  QMessageBox::Yes
                                  );

    messageBox.setButtonText(QMessageBox::Yes, tr("Ok"));

    messageBox.exec();

    if (m_barcodeInputLineEditDefault)
        ui->barcodeLineEdit->setFocus();
}

void QRKRegister::futureTimeDedected(QDateTime futuretime)
{
    QrkTimedMessageBox messageBox(20,
                                  QMessageBox::Information,
                                  tr("Datum/Uhrzeit"),
                                  tr("Datum Zeitfehler. Es gibt schon einen Bon mit Datum (%1) in der Zukunft. Überprüfen Sie bitte das Datum und die Uhrzeit des Computers.").arg(QLocale().toString(futuretime)),
                                  QMessageBox::Yes | QMessageBox::Default
                                  );

    messageBox.setDefaultButton(QMessageBox::Yes);
    messageBox.setButtonText(QMessageBox::Yes, tr("OK"));
    messageBox.exec();

    if (m_barcodeInputLineEditDefault)
        ui->barcodeLineEdit->setFocus();

}
