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

#include "documentprinter.h"
#include "database.h"
#include "utils/utils.h"
#include "utils/qrcode.h"
#include "reports.h"
#include "singleton/spreadsignal.h"
#include "RK/rk_signaturemodule.h"
#include "preferences/qrksettings.h"
#include "3rdparty/qbcmath/bcmath.h"
#include "3rdparty/ckvsoft/ckvtemplate.h"

#include <QApplication>
#include <QJsonObject>
#include <QJsonArray>
#include <QPainter>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QAbstractTextDocumentLayout>
#include <QStandardPaths>
#include <QPrinter>
#include <QTextDocument>
#include <QDebug>

DocumentPrinter::DocumentPrinter(QObject *parent)
    :QObject(parent)
{

    QrkSettings settings;
    QList<QString> printerFontList = settings.value("printerfont", "Courier-New,10,100").toString().split(",");
    QList<QString> receiptPrinterFontList = settings.value("receiptprinterfont", "Courier-New,8,100").toString().split(",");

    m_noPrinter = settings.value("noPrinter", false).toBool();
    if (settings.value("receiptPrinter", "").toString().isEmpty())
        m_noPrinter = true;

    m_pdfPrinterPath = settings.value("pdfDirectory", QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+ "/pdf").toString();
    m_printerFont = QFont(printerFontList.at(0));
    m_printerFont.setPointSize(printerFontList.at(1).toInt());
    m_printerFont.setStretch(printerFontList.at(2).toInt());
    m_printCollectionsReceipt = settings.value("printCollectionReceipt", false).toBool();
    m_collectionsReceiptText  = settings.value("collectionReceiptText", tr("Abholbon für")).toString();
    m_collectionsReceiptCopies = settings.value("collectionReceiptCopies", 1).toInt();

    m_defaultPaperWidth = settings.value("paperWidth", 80).toInt();
    m_receiptPrinterFont = QFont(receiptPrinterFontList.at(0));
    m_receiptPrinterFont.setPointSize(receiptPrinterFontList.at(1).toInt());
    m_receiptPrinterFont.setStretch(receiptPrinterFontList.at(2).toInt());

    m_printCompanyNameBold = settings.value("printCompanyNameBold", false).toBool();
    m_printQRCode = settings.value("qrcode", true).toBool();
    m_logoFileName = "";
    if (settings.value("useLogo", false).toBool())
        m_logoFileName = settings.value("logo", "logo.png").toString();

    m_advertisingFileName = "";
    if (settings.value("useAdvertising", false).toBool())
        m_advertisingFileName = settings.value("advertising", "advertising.png").toString();

    m_logoRight = settings.value("logoRight", false).toBool();
    m_numberCopies = settings.value("numberCopies", 1).toInt();
    m_paperFormat = settings.value("paperFormat", "A4").toString();
    m_currency = Database::getCurrency();
    m_smallPrinter = (settings.value("paperWidth", 80).toInt() <= 60)?true :false;

    m_feedProdukt = settings.value("feedProdukt", 5).toInt();
    m_feedCompanyHeader = settings.value("feedCompanyHeader", 5).toInt();
    m_feedCompanyAddress = settings.value("feedCompanyAddress", 5).toInt();
    m_feedCashRegisterid = settings.value("feedCashRegisterid", 5).toInt();
    m_feedTimestamp = settings.value("feedTimestamp", 5).toInt();
    m_feedTax = settings.value("feedTaxSpin", 5).toInt();
    m_feedPrintHeader = settings.value("feedPrintHeader", 5).toInt();
    m_feedHeaderText = settings.value("feedHeaderText", 5).toInt();
    m_feedQRCode = settings.value("feedQRCode", 20).toInt();

    m_printQrCodeLeft = settings.value("qrcodeleft", false).toBool();
    m_useDecimalQuantity = settings.value("useDecimalQuantity", false).toBool();
    m_countDigits = settings.value("decimalDigits", 2).toInt();

    if (settings.value("noPrinter", false).toBool()) {
        m_useReportPrinter = false;
    } else {
        m_useReportPrinter = settings.value("useReportPrinter", false).toBool();
        if (m_numberCopies > 1 && m_useReportPrinter) {
            m_numberCopies = 1;
        } else {
            m_useReportPrinter = false;
        }
    }
}

DocumentPrinter::~DocumentPrinter()
{
    Spread::Instance()->setProgressBarValue(-1);
}

//--------------------------------------------------------------------------------

void DocumentPrinter::printTestDocument(QFont font)
{
    QTextDocument testDoc;
    testDoc.setHtml(Reports::getReport(2, true));
    testDoc.setDefaultFont(font);
    printDocument(&testDoc, "TEST DRUCK");
}

void DocumentPrinter::printDocument(QTextDocument *document, QString title)
{

    Spread::Instance()->setProgressBarWait(true);

    QPrinter printer;
    QrkSettings settings;
    bool usePDF = settings.value("reportPrinterPDF", false).toBool();
    if (usePDF) {
        printer.setOutputFormat(QPrinter::PdfFormat);
        QDir dir(m_pdfPrinterPath);
        if (!dir.exists()) {
            dir.mkpath(".");
        }
    }

    document->setDefaultFont(m_printerFont);

    if ( m_noPrinter || printer.outputFormat() == QPrinter::PdfFormat) {
        initAlternatePrinter(printer);
        QString confname = qApp->property("configuration").toString();
        if (!confname.isEmpty())
            confname = "_" + confname;
        printer.setOutputFileName(QString(m_pdfPrinterPath + "/QRK%1-REPORT_%2.pdf").arg(confname).arg( title ));
        document->adjustSize();

    } else {
        initAlternatePrinter(printer);
        printer.setPrinterName(settings.value("reportPrinter").toString());
        document->adjustSize();
    }

    if (settings.value("paperFormat").toString() == "POS") {
        int height = document->size().height();
        int paperHeight = settings.value("paperHeight", 297).toInt();
        if (height > paperHeight)
            printer.setPaperSize(QSizeF(settings.value("paperWidth", 80).toInt(),
                                        height), QPrinter::Millimeter);

        QSizeF size(printer.width(),height);
        document->setPageSize(size);
        QPainter painter(&printer);
        document->documentLayout()->setPaintDevice(painter.device());
        document->drawContents(&painter);
    } else {
        document->print(&printer);
    }

    Spread::Instance()->setProgressBarWait(false);
}

void DocumentPrinter::printReceipt(QJsonObject data)
{
    // print receipt
    QPrinter printer;
    //    printer.setResolution(600);

    if (data.value("isCopy").toBool())
        m_numberCopies = 1;

    m_receiptNum = data.value("receiptNum").toInt();
    Spread::Instance()->setProgressBarValue(1);

    if (data.value("isInvoiceCompany").toBool()) {
        if ( initInvoiceCompanyPrinter(printer) )
            printI(data, printer );

    } else {

        if ( initPrinter(printer) )
            printI(data, printer );

        /* Some Printdriver do not accept more than 1 print.
     * so we send a second printjob
     */
        if (m_numberCopies > 1) {
            m_printCollectionsReceipt = false; //we finish this by first print
            if (m_useReportPrinter)
                initAlternatePrinter(printer);

            printI(data, printer );
        }
    }
}

void DocumentPrinter::printTagged(QJsonObject data)
{
    QPrinter printer;
    QrkSettings settings;

    QString description = data.value("customerText").toString();
    if (description.isEmpty())
        return;

    int printerid = data.value("printer").toInt();
    QString format = "POS";

    switch (printerid) {
    case 0:
        printer.setPrinterName(settings.value("collectionPrinter").toString());
        format = settings.value("collectionPrinterPaperFormat", "POS").toString();
        break;
    default:
        if ( m_noPrinter || printer.outputFormat() == QPrinter::PdfFormat) {
            QString confname = qApp->property("configuration").toString();
            if (!confname.isEmpty())
                confname = "_" + confname;
            printer.setOutputFileName(QString(m_pdfPrinterPath + "/QRK%1-BON%2.pdf").arg(confname).arg( description ));
        } else {
            printer.setPrinterName(settings.value("receiptPrinter").toString());
        }
        break;
    }

    if (format == "A4")
        printer.setPaperSize(printer.A4);
    if (format == "A5")
        printer.setPaperSize(printer.A5);
    if (format == "POS") {
        printer.setFullPage(true);
        printer.setPaperSize(QSizeF(settings.value("paperWidth", 80).toInt(),
                                    settings.value("paperHeight", 210).toInt()), QPrinter::Millimeter);

        const QMarginsF marginsF(settings.value("marginLeft", 0).toDouble(),
                                 settings.value("marginTop", 17).toDouble(),
                                 settings.value("marginRight", 5).toDouble(),
                                 settings.value("marginBottom", 0).toDouble());

        printer.setPageMargins(marginsF,QPageLayout::Millimeter);
        printer.setFullPage(false);
        qInfo() << "Function Name: " << Q_FUNC_INFO << " margins:" << printer.pageLayout().margins();
    }

    QFont font(m_receiptPrinterFont);

    QFont boldFont(m_receiptPrinterFont);
    boldFont.setBold(true);

    QFontMetrics boldMetr(boldFont);

    QFont numFont(m_receiptPrinterFont);
    numFont.setBold(true);
    numFont.setPointSize(numFont.pointSize() * 3);

    QFontMetrics sumMetr(numFont);

    const int WIDTH = printer.pageRect().width();
    int y = 0;


    QPainter painter(&printer);
    painter.setFont(font);
    QPen pen(Qt::black);
    painter.setPen(pen);
    QFontMetrics fontMetr = painter.fontMetrics();

    y = 0;

    painter.save();
    painter.setFont(numFont);
    // CustomerText
    QString headerText = data.value("customerText").toString();
    headerText = Utils::wordWrap(headerText, WIDTH, numFont);
    int headerTextHeight = headerText.split(QRegExp("\n|\r\n|\r")).count() * sumMetr.height();
    painter.drawText(0, y, WIDTH, headerTextHeight + 4, Qt::AlignCenter, headerText);
    y += 5 + headerTextHeight;
    painter.restore();

    painter.drawText(0, y, WIDTH, fontMetr.height(), Qt::AlignCenter, tr("Datum: %1 Uhrzeit: %2")
                     .arg(QDateTime::currentDateTime().toString("dd.MM.yyyy"))
                     .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    y += 5 + fontMetr.height();

    painter.save();
    painter.setFont(boldFont);

    QJsonArray Orders = data["items"].toArray();
    foreach (const QJsonValue & item, Orders)
    {
        const QJsonObject& order = item.toObject();
        QString product = QString("%1 x %2").arg(order.value("count").toString()).arg(order.value("name").toString());
        product = Utils::wordWrap(product, WIDTH, boldFont);
        int productHeight = product.split(QRegExp("\n|\r\n|\r")).count() * boldMetr.height();
        painter.drawText(0,  y + 10, WIDTH,  productHeight, Qt::AlignLeft, product);
        y += 5 + productHeight;
    }
    painter.end();
}

void DocumentPrinter::printCollectionReceipt(QJsonObject data, QPrinter &printer)
{

    QrkSettings settings;
    QString collectionPrinter = settings.value("collectionPrinter").toString();
    if ( m_noPrinter || printer.outputFormat() == QPrinter::PdfFormat || collectionPrinter.isEmpty()) {
        QString confname = qApp->property("configuration").toString();
        if (!confname.isEmpty())
            confname = "_" + confname;
        printer.setOutputFileName(QString(m_pdfPrinterPath + "/QRK%1-BON%2-ABHOLBON.pdf").arg(confname).arg( m_receiptNum ));
    } else {
        printer.setPrinterName(collectionPrinter);
    }

    QString f = settings.value("collectionPrinterPaperFormat", "POS").toString();
    if (f == "A4")
        printer.setPaperSize(printer.A4);
    if (f == "A5")
        printer.setPaperSize(printer.A5);
    if (f == "POS") {
        printer.setFullPage(true);
        printer.setPaperSize(QSizeF(settings.value("paperWidth", 80).toInt(),
                                    settings.value("paperHeight", 210).toInt()), QPrinter::Millimeter);

        const QMarginsF marginsF(settings.value("marginLeft", 0).toDouble(),
                                 settings.value("marginTop", 17).toDouble(),
                                 settings.value("marginRight", 5).toDouble(),
                                 settings.value("marginBottom", 0).toDouble());

        printer.setPageMargins(marginsF,QPageLayout::Millimeter);
        printer.setFullPage(false);
        qInfo() << "Function Name: " << Q_FUNC_INFO << " margins:" << printer.pageLayout().margins();
    }

    QFont font(m_receiptPrinterFont);

    QFont boldFont(m_receiptPrinterFont);
    boldFont.setBold(true);

    QFontMetrics boldMetr(boldFont);

    QFont numFont(m_receiptPrinterFont);
    numFont.setBold(true);
    numFont.setPointSize(numFont.pointSize() * 3);

    QFontMetrics sumMetr(numFont);

    const int WIDTH = printer.pageRect().width();
    int y = 0;

    QString shopName = data.value("shopName").toString();
    QString shopMasterData = data.value("shopMasterData").toString();
    QString receiptNumber = QString::number(data.value("receiptNum").toInt());

    QJsonArray Orders = data["Orders"].toArray();
    foreach (const QJsonValue & item, Orders)
    {

        const QJsonObject& order = item.toObject();

        QBCMath count = order.value("count").toDouble();
        count.round(m_countDigits);

        bool coupon = order.value("coupon").toString() == "1";
        if (!coupon)
            continue;

        for (int i = 0; i< m_collectionsReceiptCopies; i++) {
            QPainter painter(&printer);
            painter.setFont(font);
            QPen pen(Qt::black);
            painter.setPen(pen);
            QFontMetrics fontMetr = painter.fontMetrics();

            y = 0;

            painter.save();
            painter.setFont(numFont);
            painter.drawText(0, y, WIDTH, sumMetr.height() + 4, Qt::AlignCenter, receiptNumber);
            y += 5 + sumMetr.height();
            painter.restore();

            if (m_printCompanyNameBold) {
                painter.save();
                painter.setFont(boldFont);
                painter.drawText(0, y, WIDTH, boldMetr.height() + 4, Qt::AlignCenter, shopName);
                y += 5;
                painter.restore();
            } else {
                painter.drawText(0, y, WIDTH, fontMetr.height() + 4, Qt::AlignCenter, shopName);
                y += 5;
            }

            int shopMasterDataHeight = shopMasterData.split(QRegExp("\n|\r\n|\r")).count() * fontMetr.height();
            painter.drawText(0, y, WIDTH, shopMasterDataHeight + 4, Qt::AlignCenter, shopMasterData);
            y += 15 + shopMasterDataHeight;

            painter.drawText(0, y, WIDTH, fontMetr.height(), Qt::AlignCenter, tr("Datum: %1 Uhrzeit: %2")
                             .arg(QDateTime::fromString(data.value("receiptTime").toString(), Qt::ISODate).toString("dd.MM.yyyy"))
                             .arg(QDateTime::fromString(data.value("receiptTime").toString(), Qt::ISODate).toString("hh:mm:ss")));
            y += 5 + fontMetr.height();

            painter.save();
            painter.setFont(boldFont);

            // CustomerText
            if (! data.value("headerText").toString().isEmpty()) {
                y += 5;
                QString headerText = data.value("headerText").toString();
                headerText = Utils::wordWrap(headerText, WIDTH, font);
                int headerTextHeight = headerText.split(QRegExp("\n|\r\n|\r")).count() * fontMetr.height();
                painter.drawText(0, y, WIDTH, headerTextHeight, Qt::AlignCenter, headerText);
                y += m_feedHeaderText + headerTextHeight + 4;
                painter.drawLine(0, y, WIDTH, y);
                y += 5;
            }

            y += 5 + boldMetr.height();
            painter.drawText(0, y, WIDTH, boldMetr.height(), Qt::AlignCenter, m_collectionsReceiptText);
            y += 5 + boldMetr.height() * 2;

            QString product = QString("%1 x %2").arg(count.toString()).arg(order.value("product").toString());
            product = Utils::wordWrap(product, WIDTH, boldFont);
            int productHeight = product.split(QRegExp("\n|\r\n|\r")).count() * boldMetr.height();
            painter.drawText(0,  y + 10, WIDTH,  productHeight, Qt::AlignCenter, product);
            painter.end();
        }
    }
}

void DocumentPrinter::printI(QJsonObject data, QPrinter &printer)
{

    int fontsize = m_receiptPrinterFont.pointSize();

    QPainter painter(&printer);
    QFont font(m_receiptPrinterFont);
    ckvTemplate Template;
    Template.set("DATUM", QDate::currentDate().toString());
    Template.set("DISPLAYNAME", data.value("displayname").toString());
    Template.set("UHRZEIT", QTime::currentTime().toString());
    Template.set("BONNUMMER", QString::number(data.value("receiptNum").toInt()));
    Template.set("VERSION", data.value("version").toString());
    QString sum = QString::number(data.value("sum").toDouble(), 'f', 2);

    if (data.value("isTestPrint").toBool()) {
        sum = "0,0";
    }

    Template.set("SUMME", sum);

    // font.setFixedPitch(true);
    painter.setFont(font);
    QFontMetrics fontMetr = painter.fontMetrics();

    QFont grossFont(font);
    // grossFont.setFixedPitch(true);
    QFontMetrics grossMetrics(grossFont, &printer);

    // QFont boldFont("Courier-New", boldsize, QFont::Bold);  // for sum
    QFont boldFont(m_receiptPrinterFont);
    boldFont.setBold(true);
    boldFont.setPointSize(m_receiptPrinterFont.pointSize() + 2);

    QFontMetrics boldMetr(boldFont);

    QPen pen(Qt::black);
    painter.setPen(pen);

    QJsonArray Orders = data["Orders"].toArray();
    int oc = Orders.count() + 100;

    const int WIDTH = printer.pageRect().width();
    const double FACTOR = getFactor(WIDTH, printer);

    int y = 0;

    bool logo = false;
    QFileInfo checkFile(m_logoFileName);
    // check if file exists and if yes: Is it really a file and no directory?
    if (checkFile.exists() && checkFile.isFile()) {
        logo = true;
    }

    bool advertising = false;
    QFileInfo checkAdvertisingFile(m_advertisingFileName);
    // check if file exists and if yes: Is it really a file and no directory?
    if (checkAdvertisingFile.exists() && checkAdvertisingFile.isFile()) {
        advertising = true;
    }

    Spread::Instance()->setProgressBarValue(((float)15 / (float)oc) * 100);

    bool isInvoiceCompany = data.value("isInvoiceCompany").toBool();

    QPixmap logoPixmap;
    QPixmap advertisingPixmap;

    QString shopName = data.value("shopName").toString();
    QString shopMasterData = data.value("shopMasterData").toString();

    if (!isInvoiceCompany) {
        if (logo) {

            logoPixmap.load(m_logoFileName);
            logoPixmap = logoPixmap.scaled(logoPixmap.size() * FACTOR, Qt::KeepAspectRatio);

            if (m_logoRight) {
                if (logoPixmap.width() > WIDTH / 2.50)
                    logoPixmap =  logoPixmap.scaled(WIDTH / 2.50, printer.pageRect().height(), Qt::KeepAspectRatio);

                painter.drawPixmap(WIDTH - logoPixmap.width() - 1, y, logoPixmap);

                QRect rect;
                if (m_printCompanyNameBold) {
                    painter.save();
                    painter.setFont(boldFont);
                    rect = painter.boundingRect(0, y, WIDTH - logoPixmap.width(), logoPixmap.height(), Qt::AlignLeft, shopName);
                    painter.drawText(0, y, rect.width(), rect.height(), Qt::AlignLeft, shopName);
                    y += 5;
                    painter.restore();
                } else {
                    rect = painter.boundingRect(0, y, WIDTH - logoPixmap.width(), logoPixmap.height(), Qt::AlignLeft, shopName);
                    painter.drawText(0, y, rect.width(), rect.height(), Qt::AlignLeft, shopName);
                    y += 5;
                }

                rect = painter.boundingRect(0, y, WIDTH - logoPixmap.width(), logoPixmap.height(), Qt::AlignLeft, shopMasterData);
                painter.drawText(0, y, rect.width(), rect.height(), Qt::AlignLeft, shopMasterData);

                y += 5 + qMax(rect.height(), logoPixmap.height()) + 4;
                painter.drawLine(0, y, WIDTH, y);
                y += 5;
            } else {

                if (logoPixmap.width() > WIDTH)
                    logoPixmap =  logoPixmap.scaled(WIDTH, printer.pageRect().height(), Qt::KeepAspectRatio);
                painter.drawPixmap((WIDTH / 2) - (logoPixmap.width()/2) - 1, y, logoPixmap);
                y += 5 + logoPixmap.height() + 4;

                if (m_printCompanyNameBold) {
                    painter.save();
                    painter.setFont(boldFont);
                    painter.drawText(0, y, WIDTH, boldMetr.height() + 4, Qt::AlignCenter, shopName);
                    y += 5;
                    painter.restore();

                } else {
                    painter.drawText(0, y, WIDTH, fontMetr.height() + 4, Qt::AlignCenter, shopName);
                    y += 5;
                }

                int shopMasterDataHeight = shopMasterData.split(QRegExp("\n|\r\n|\r")).count() * fontMetr.height();
                painter.drawText(0, y, WIDTH, shopMasterDataHeight + 4, Qt::AlignCenter, shopMasterData);
                y += 5 + shopMasterDataHeight;

            }
        } else {
            if (m_printCompanyNameBold) {
                painter.save();
                painter.setFont(boldFont);
                painter.drawText(0, y, WIDTH, boldMetr.height() + 4, Qt::AlignCenter, shopName);
                y += m_feedCompanyHeader;
                painter.restore();
            } else {
                painter.drawText(0, y, WIDTH, fontMetr.height() + 4, Qt::AlignCenter, shopName);
                y += m_feedCompanyHeader;
            }

            int shopMasterDataHeight = shopMasterData.split(QRegExp("\n|\r\n|\r")).count() * fontMetr.height();
            painter.drawText(0, y, WIDTH, shopMasterDataHeight + 4, Qt::AlignCenter, shopMasterData);
            y += m_feedCompanyAddress + shopMasterDataHeight;

        }
    }

    if (! data.value("printHeader").toString().isEmpty()) {
        QString printHeader = data.value("printHeader").toString();
        printHeader = Utils::wordWrap(Template.process(printHeader), WIDTH, font);
        int headerTextHeight = printHeader.split(QRegExp("\n|\r\n|\r")).count() * fontMetr.height();
        painter.drawText(0, y, WIDTH, headerTextHeight, Qt::AlignCenter, printHeader);
        y += m_feedPrintHeader + headerTextHeight + 4;
        painter.drawLine(0, y, WIDTH, y);
    }

    // CustomerText
    if (! data.value("headerText").toString().isEmpty()) {
        y += 5;
        QString headerText = data.value("headerText").toString();
        headerText = Utils::wordWrap(headerText, WIDTH, font);
        int headerTextHeight = headerText.split(QRegExp("\n|\r\n|\r")).count() * fontMetr.height();
        painter.drawText(0, y, WIDTH, headerTextHeight, Qt::AlignLeft, headerText);
        y += m_feedHeaderText + headerTextHeight + 4;
        painter.drawLine(0, y, WIDTH, y);
        y += 5;
    }

    Spread::Instance()->setProgressBarValue(((float)20 / (float)oc) * 100);

    painter.save();
    painter.setFont(boldFont);

    // receiptPrinterHeading or cancellationtext by cancellation
    QString comment = data.value("comment").toString();
    if (!comment.isEmpty()) {
        y += 5 + boldMetr.height();
        painter.drawText(0, y, WIDTH, boldMetr.height(), Qt::AlignCenter, comment);
        y += 5 + boldMetr.height() * 2;
    }

    painter.restore();

    QString copy = "";
    if (data.value("isCopy").toBool())
        copy = tr("( Kopie )");

    if (data.value("isTestPrint").toBool()) {
        data["kassa"] = "DEMO-PRINT-1";
        data["receiptNum"] = 0;
        data["typeText"] = "DEMO";
    }

    if (m_smallPrinter) {
        painter.drawText(0, y, WIDTH, fontMetr.height(), Qt::AlignLeft,
                         tr("Kasse: %1")
                         .arg(data.value("kasse").toString()));
        y += m_feedCashRegisterid + fontMetr.height();

        painter.drawText(0, y, WIDTH, fontMetr.height(), Qt::AlignLeft,
                         tr("Bon-Nr: %1 %2 - %3")
                         .arg(data.value("receiptNum").toInt())
                         .arg(copy).arg(data.value("typeText").toString()));
        y += m_feedCashRegisterid + fontMetr.height();

        painter.drawText(0, y, WIDTH, fontMetr.height(), Qt::AlignLeft,
                         tr("Positionen: %1")
                         .arg(data.value("positions").toInt()));
        y += m_feedCashRegisterid + fontMetr.height();

        Spread::Instance()->setProgressBarValue(((float)30 / (float)oc) * 100);

        painter.drawText(0, y, WIDTH, fontMetr.height(), Qt::AlignLeft, tr("Datum: %1")
                         .arg(QDateTime::fromString(data.value("receiptTime").toString(), Qt::ISODate).toString("dd.MM.yyyy")));
        y += m_feedTimestamp + fontMetr.height();

        painter.drawText(0, y, WIDTH, fontMetr.height(), Qt::AlignLeft, tr("Uhrzeit: %1")
                         .arg(QDateTime::fromString(data.value("receiptTime").toString(), Qt::ISODate).toString("hh:mm:ss")));
        y += m_feedTimestamp + fontMetr.height();

    } else {
        painter.drawText(0, y, WIDTH, fontMetr.height(), Qt::AlignLeft,
                         tr("Kasse: %1 Positionen: %2")
                         .arg(data.value("kasse").toString())
                         .arg(data.value("positions").toInt()));
        // TODO: 1 or 2 Lines from config, in my case the text is to long. Do not fit in one line
        y += m_feedCashRegisterid + fontMetr.height();

        painter.drawText(0, y, WIDTH, fontMetr.height(), Qt::AlignLeft,
                         tr("Bon-Nr: %1 %2 - %3")
                         .arg(data.value("receiptNum").toInt())
                         .arg(copy).arg(data.value("typeText").toString()));
        y += m_feedCashRegisterid + fontMetr.height();


        painter.drawText(0, y, WIDTH, fontMetr.height(), Qt::AlignLeft, tr("Datum: %1 Uhrzeit: %2")
                         .arg(QDateTime::fromString(data.value("receiptTime").toString(), Qt::ISODate).toString("dd.MM.yyyy"))
                         .arg(QDateTime::fromString(data.value("receiptTime").toString(), Qt::ISODate).toString("hh:mm:ss")));
        y += m_feedTimestamp + fontMetr.height();

    }

    Spread::Instance()->setProgressBarValue(((float)50 / (float)oc) * 100);

    painter.drawLine(0, y, WIDTH, y);
    y += 5;

    // paint orders

    QString countText = tr("Anz");

    // get maximum counter size
    int counter_size = 0;
    foreach (const QJsonValue & item, Orders)
    {
        const QJsonObject& order = item.toObject();
        QBCMath count;
        if (m_useDecimalQuantity) {
            count = order.value("count").toDouble();
            count.round(m_countDigits);
        } else {
            count = order.value("count").toInt();
        }

        counter_size = qMax(counter_size, count.toString().size());
    }

    int bountingWidth = qMax(fontMetr.boundingRect(countText + " ").width(), fontMetr.boundingRect(QString("0").repeated(counter_size) +" ").width());
    const int X_COUNT = 0;
    const int X_NAME  = bountingWidth > 25? bountingWidth: 25;

    painter.drawText(X_COUNT, y, WIDTH, fontMetr.height(), Qt::AlignLeft, countText);
    painter.drawText(X_NAME,  y, WIDTH - X_COUNT,  fontMetr.height(), Qt::AlignLeft, tr("Artikel"));
    painter.drawText(0, y, WIDTH, fontMetr.height(), Qt::AlignRight, tr("Preis  M%"));
    y += 5 + fontMetr.height();

    Spread::Instance()->setProgressBarValue(((float)60 / (float)oc) * 100);

    int progress = 60;

    foreach (const QJsonValue & item, Orders)
    {

        Spread::Instance()->setProgressBarValue(((float)progress++ / (float)oc) * 100);

        const QJsonObject& order = item.toObject();

        QBCMath count;
        if (m_useDecimalQuantity) {
            count = order.value("count").toDouble();
            count.round(m_countDigits);
        } else {
            count = order.value("count").toInt();
        }

        QString taxPercent;

        if (Database::getTaxLocation() == "CH")
            taxPercent = QString("%1").arg(QString::number(order.value("tax").toDouble(),'f',2));
        else
            taxPercent = QString("%1").arg(QString::number(order.value("tax").toInt()));

        if (taxPercent == "0") taxPercent = "00";

        QBCMath discount = order.value("discount").toDouble();
        discount.round(2);

        QBCMath gross = order.value("gross").toDouble();
        gross.round(2);

        QBCMath singleprice = order.value("singleprice").toDouble();
        singleprice.round(2);

        QString grossText = QString("%1").arg(gross.toString().replace(".",","));
        QString singleGrossText;
        if (discount > 0)
            singleGrossText = QString("%1 x %2 Rabatt:\u00A0-%3%").arg(count.toString()).arg(singleprice.toString().replace(".",",")).arg(discount.toString());
        else
            singleGrossText = QString("%1 x %2").arg(count.toString().replace(".",",")).arg(singleprice.toString().replace(".",","));

        if (data.value("isTestPrint").toBool()) {
            count = 0.0;
            grossText = "0,0";
            singleGrossText = "0 x 0,0";
        }

        int grossWidth = grossMetrics.boundingRect(grossText + "   " + taxPercent).width();
        QString product = order.value("product").toString();
        product = Utils::wordWrap(product, WIDTH - grossWidth - X_NAME, font);
        int productHeight = product.split(QRegExp("\n|\r\n|\r")).count() * fontMetr.height();

        // check if new drawText is heigher than page height
        if ( (y + productHeight + 20) > printer.pageRect().height() )
        {
            printer.newPage();
            y = 0;
        }

        QRect usedRect;
        QString countString;
        if (m_useDecimalQuantity)
            countString = count.toString().replace(".", ",");
        else
            countString = QString::number(count.toInt()).replace(".", ",");

        painter.drawText(X_COUNT, y, WIDTH - X_COUNT, fontMetr.height(), Qt::AlignLeft, countString);
        painter.drawText(X_NAME,  y, WIDTH,  productHeight, Qt::AlignLeft, product, &usedRect);

        if (m_useDecimalQuantity || discount.toDouble() > 0 || count.toDouble() > 1 || count.toDouble() < -1) {
            y += m_feedProdukt + usedRect.height();
            singleGrossText = Utils::wordWrap(singleGrossText, WIDTH - grossWidth - X_NAME, font);
            int singleGrossTextHeight = singleGrossText.split(QRegExp("\n|\r\n|\r")).count() * fontMetr.height();
            painter.drawText(X_NAME,  y, WIDTH - X_NAME - grossWidth - 5, singleGrossTextHeight, Qt::AlignLeft, singleGrossText, &usedRect);
            y += usedRect.height() - fontMetr.height();
        } else {
            y += usedRect.height() - fontMetr.height();
        }

        painter.setFont(grossFont);
        painter.drawText(0, y, WIDTH, fontMetr.height(), Qt::AlignRight, grossText + "   " + taxPercent);
        painter.setFont(font);

        y += m_feedProdukt + fontMetr.height();
    }

    Spread::Instance()->setProgressBarValue(((float)progress +10 / (float)oc) * 100);

    painter.drawLine(0, y, WIDTH, y);
    y += fontMetr.height() / 2;

    // if there is not enough space for sum+tax lines, start new page
    if ( (y + (data.value("taxesCount").toInt() * (5 + fontMetr.height())) + boldMetr.height() + 10) > printer.pageRect().height() )
    {
        printer.newPage();
        y = 0;
    }

    Spread::Instance()->setProgressBarValue(((float)(progress += 10) / (float)oc) * 100);

    int ySave = y; // save y when QR-Code was printing left
    QString sumText = tr("Gesamt: %1").arg(sum).replace(".",",");
    painter.save();
    painter.setFont(boldFont);

    painter.drawText(0, y, WIDTH, boldMetr.height(), Qt::AlignRight, sumText);
    painter.restore();

    y += 5 + boldMetr.height();

    QJsonArray Taxes = data["Taxes"].toArray();
    foreach (const QJsonValue & item, Taxes)
    {
        const QJsonObject& tax = item.toObject();

//        QString taxSum = QString::number(tax.value("t2").toString().toDouble(), 'f', 2).replace(".",",");
        QBCMath taxSum = tax.value("t2").toString();
        taxSum.round(2);
        if (data.value("isTestPrint").toBool()) {
            taxSum = "0,0";
        }

        QString taxValue = tax.value("t1").toString();
        if (taxValue != "0%"){
            painter.drawText(0, y, WIDTH, fontMetr.height(), Qt::AlignRight,
                             tr("MwSt %1: %2")
                             .arg(taxValue)
                             .arg(taxSum.toString()));

            y += m_feedTax + fontMetr.height();
        }
    }
    y += 5 + fontMetr.height();

    // Die Währung müsste sonst neben jeden Preis stehen, darum schreiben wir diesen InfoText
    QString currencyText = tr("(Alle Beträge in %1)").arg(m_currency);
    if (m_printQRCode && m_printQrCodeLeft) {
        painter.drawText(0, y, WIDTH, fontMetr.height(), Qt::AlignRight, currencyText);
        y += 20;
    } else {
        painter.drawText(0, y, WIDTH, fontMetr.height(), Qt::AlignCenter, currencyText);
    }

    y += 5 + fontMetr.height();

    Spread::Instance()->setProgressBarValue(((float)(progress += 10) / (float)oc) * 100);

    QString qr_code_rep = shopName + " - " + shopMasterData;
    QString ocr_code_rep = shopName + " - " + shopMasterData;

    if (RKSignatureModule::isDEPactive()) {
        QString signature = Utils::getReceiptSignature(data.value("receiptNum").toInt(), true);
        if (signature.split('.').size() == 3) {
            qr_code_rep = signature.split('.').at(1);
            qr_code_rep = RKSignatureModule::base64Url_decode(qr_code_rep);
            ocr_code_rep = qr_code_rep;
            qr_code_rep = qr_code_rep + "_" + RKSignatureModule::base64Url_decode(signature.split('.').at(2)).toBase64();
            ocr_code_rep = ocr_code_rep + "_" + RKSignatureModule::base32_encode(RKSignatureModule::base64Url_decode(signature.split('.').at(2)));
            if (signature.split('.').at(2) == RKSignatureModule::base64Url_encode("Sicherheitseinrichtung ausgefallen"))
                data["isSEEDamaged"] = true;
            qDebug() << "Function Name: " << Q_FUNC_INFO << " QRCode Representation: " << qr_code_rep;
        } else {
            qInfo() << "Function Name: " << Q_FUNC_INFO << " Print old (before DEP-7) Receipt Id:" << data.value("receiptNum").toInt();
        }
    }

    if (m_printQRCode && m_printQrCodeLeft) {
        QRCode qr;
        QPixmap QR = qr.encodeTextToPixmap(qr_code_rep);
        QR = QR.scaled(QR.size() * FACTOR, Qt::KeepAspectRatio);

        int sumWidth = boldMetr.boundingRect(sumText).width();

        if (QR.width() > (WIDTH - sumWidth))
            QR =  QR.scaled(WIDTH - sumWidth - 4, printer.pageRect().height(), Qt::KeepAspectRatio);

        painter.drawPixmap( 1, ySave, QR);

        y = m_feedQRCode + qMax(ySave + QR.height(), y);

        if(data.value("isSEEDamaged").toBool()) {
            y += 5 + fontMetr.height();
            painter.drawText(0, y, WIDTH, fontMetr.height(), Qt::AlignCenter, "Sicherheitseinrichtung ausgefallen");
            y += 5 + fontMetr.height();
        }
        painter.drawPoint(0,y);
    }

    if(isInvoiceCompany) {
        y += 5;
        QString printFooter = tr("Dient als Steuerbeleg für ihr Finanzamt.\n"
                                 "Wichtig: Beleg bitte aufbewahren.\n"
                                 "Diese Rechnung ist nur in Verbindung\n"
                                 "mit dem angehefteten Kassenbon gültig.\n"
                                 ">> Datum = Liefer- und Rechnungsdatum <<\n");

        int headerTextHeight = printFooter.split(QRegExp("\n|\r\n|\r")).count() * fontMetr.height();
        painter.drawText(0, y, WIDTH, headerTextHeight, Qt::AlignCenter, printFooter);
        y += 5 + headerTextHeight + 4;

    }
    else if (! data.value("printFooter").toString().isEmpty()) {
        y += 5;
        QString printFooter = data.value("printFooter").toString();
        printFooter = Utils::wordWrap(Template.process(printFooter), WIDTH, font);
        int headerTextHeight = printFooter.split(QRegExp("\n|\r\n|\r")).count() * fontMetr.height();
        painter.drawText(0, y, WIDTH, headerTextHeight, Qt::AlignCenter, printFooter);
        y += 5 + headerTextHeight + 4;
    }

    Spread::Instance()->setProgressBarValue(((float)(progress += 10) / (float)oc) * 100);

    if (m_printQRCode && !m_printQrCodeLeft) {
        QRCode qr;
        QPixmap QR = qr.encodeTextToPixmap(qr_code_rep);
        QR = QR.scaled(QR.size() * FACTOR, Qt::KeepAspectRatio);

        if (QR.width() > WIDTH) {
            QR =  QR.scaled(WIDTH, printer.pageRect().height(), Qt::KeepAspectRatio);
        }

        y += 5;
        painter.drawLine(0, y, WIDTH, y);
        y += 5;

        // check if new drawText is heigher than page height
        if ( (y + QR.height() + 20) > printer.pageRect().height() )
        {
            printer.newPage();
            y = 0;
        }
        painter.drawPixmap((WIDTH / 2) - (QR.width()/2) - 1, y, QR);

        y += QR.height() + m_feedQRCode;

    } else if (!m_printQRCode && Database::getTaxLocation() == "AT") {
        y += 5;
        painter.drawLine(0, y, WIDTH, y);
        y += 5;

        int id = QFontDatabase::addApplicationFont(":src/font/ocra.ttf");
        QString family = QFontDatabase::applicationFontFamilies(id).at(0);
        QFont ocrfont(family, fontsize);
        painter.save();
        painter.setFont(ocrfont);

        QFontMetrics ocrMetr = painter.fontMetrics();

        ocr_code_rep = Utils::wordWrap(ocr_code_rep, WIDTH, ocrfont);
        int ocrHeight = ocr_code_rep.split(QRegExp("\n|\r\n|\r")).count() * ocrMetr.height();

        // check if new drawText is heigher than page height
        if ( (y + ocrHeight + m_feedQRCode) > printer.pageRect().height() )
        {
            printer.newPage();
            y = 0;
        }

        painter.drawText(0,  y, WIDTH,  ocrHeight, Qt::AlignLeft, ocr_code_rep);
        y += ocrHeight + m_feedQRCode;
        painter.restore();
    }

    if (!m_printQrCodeLeft) {
        if(data.value("isSEEDamaged").toBool()) {
            y += 5 + fontMetr.height();
            painter.drawText(0, y, WIDTH, fontMetr.height(), Qt::AlignCenter, "Sicherheitseinrichtung ausgefallen");
            y += 5 + fontMetr.height();
        }
        painter.drawPoint(0,y);
    }

    if (advertising) {
        y += 5;
        if ( (y + advertisingPixmap.height() + 20) > printer.pageRect().height() )
        {
            printer.newPage();
            y = 0;
        }

        advertisingPixmap.load(m_advertisingFileName);
        advertisingPixmap = advertisingPixmap.scaled(advertisingPixmap.size() * FACTOR, Qt::KeepAspectRatio);

        if (advertisingPixmap.width() > WIDTH)
            advertisingPixmap = advertisingPixmap.scaled(WIDTH, printer.pageRect().height(), Qt::KeepAspectRatio);
        painter.drawPixmap((WIDTH / 2) - (advertisingPixmap.width()/2) - 1, y, advertisingPixmap);
        y += 5 + advertisingPixmap.height() + 4;
    }

    if (! data.value("printAdvertisingText").toString().isEmpty()) {
        QString printAdvertisingText = data.value("printAdvertisingText").toString();
        printAdvertisingText = Utils::wordWrap(Template.process(printAdvertisingText), WIDTH, font);
        int headerTextHeight = printAdvertisingText.split(QRegExp("\n|\r\n|\r")).count() * fontMetr.height();
        painter.drawText(0, y, WIDTH, headerTextHeight, Qt::AlignCenter, printAdvertisingText);
        y += headerTextHeight;
    }

    // workaround marginBottom
    int marginBottom = printer.margins().bottom;
    if (marginBottom > 0) {
        y += marginBottom;
        painter.drawPoint(0,y);
    }

    painter.end();

    Spread::Instance()->setProgressBarValue(100);

    if (m_printCollectionsReceipt)
        printCollectionReceipt(data, printer);

}

//--------------------------------------------------------------------------------

bool DocumentPrinter::initPrinter(QPrinter &printer)
{
    QrkSettings settings;
    if ( m_noPrinter || printer.outputFormat() == QPrinter::PdfFormat) {
        QString confname = qApp->property("configuration").toString();
        if (!confname.isEmpty())
            confname = "_" + confname;
        printer.setOutputFileName(QString(m_pdfPrinterPath + "/QRK%1-BON%2.pdf").arg(confname).arg( m_receiptNum ));
    } else {
        printer.setPrinterName(settings.value("receiptPrinter").toString());
    }

    printer.setFullPage(true);

    // printer.setResolution(720);

    printer.setPaperSize(QSizeF(settings.value("paperWidth", 80).toInt(),
                                settings.value("paperHeight", 210).toInt()), QPrinter::Millimeter);

    const QMarginsF marginsF(settings.value("marginLeft", 0).toDouble(),
                             settings.value("marginTop", 17).toDouble(),
                             settings.value("marginRight", 5).toDouble(),
                             settings.value("marginBottom", 0).toDouble());

    printer.setPageMargins(marginsF,QPageLayout::Millimeter);

    printer.setFullPage(false);
    qDebug() << "Function Name: " << Q_FUNC_INFO << " margins:" << printer.pageLayout().margins();
    return true;
}

bool DocumentPrinter::initAlternatePrinter(QPrinter &printer)
{
    QrkSettings settings;
    printer.setPrinterName(settings.value("reportPrinter").toString());

    QString f = settings.value("paperFormat").toString();
    if (f == "A4")
        printer.setPaperSize(printer.A4);
    if (f == "A5")
        printer.setPaperSize(printer.A5);
    if (f == "POS") {
        printer.setFullPage(true);
        printer.setPaperSize(QSizeF(settings.value("paperWidth", 80).toInt(),
                                    settings.value("paperHeight", 210).toInt()), QPrinter::Millimeter);

        const QMarginsF marginsF(settings.value("marginLeft", 0).toDouble(),
                                 settings.value("marginTop", 17).toDouble(),
                                 settings.value("marginRight", 5).toDouble(),
                                 settings.value("marginBottom", 0).toDouble());

        printer.setPageMargins(marginsF,QPageLayout::Millimeter);
        printer.setFullPage(false);
        qDebug() << "Function Name: " << Q_FUNC_INFO << " margins:" << printer.pageLayout().margins();
    }

    return true;
}

bool DocumentPrinter::initInvoiceCompanyPrinter(QPrinter &printer)
{
    QrkSettings settings;
    printer.setPrinterName(settings.value("invoiceCompanyPrinter").toString());

    QString f = settings.value("invoiceCompanyPaperFormat").toString();
    if (f == "A4")
        printer.setPaperSize(printer.A4);
    if (f == "A5")
        printer.setPaperSize(printer.A5);

    printer.setFullPage(true);

    const QMarginsF marginsF(settings.value("invoiceCompanyMarginLeft", 90).toDouble(),
                             settings.value("invoiceCompanyMarginTop", 50).toDouble(),
                             settings.value("invoiceCompanyMarginRight", 10).toDouble(),
                             settings.value("invoiceCompanyMarginBottom", 0).toDouble());

    printer.setPageMargins(marginsF,QPageLayout::Millimeter);

    printer.setFullPage(false);

    return true;
}

double DocumentPrinter::getFactor(int pixel, QPrinter &printer)
{
    const double p = 3.779527559055;
    int rect = printer.pageRect(QPrinter::Millimeter).width();
    int ratio = pixel / p;
    return (double) ratio / rect;
}
