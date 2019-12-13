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
#include "utils/utils.h"
#include "reports.h"
#include "documentprinter.h"
#include "backup.h"
#include "export.h"
#include "qrkprogress.h"
#include "singleton/spreadsignal.h"
#include "RK/rk_signaturemodule.h"
#include "3rdparty/qbcmath/bcmath.h"
#include "preferences/qrksettings.h"
#include "defines.h"

#include <QApplication>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QSqlError>
#include <QMessageBox>
#include <QJsonObject>
#include <QTextDocument>
#include <QTextCursor>
#include <QDebug>

Reports::Reports(QObject *parent, bool servermode)
    : ReceiptItemModel(parent), m_servermode(servermode)
{
}

//--------------------------------------------------------------------------------

Reports::~Reports()
{
    Spread::Instance()->setProgressBarValue(-1);
}

/**
 * @brief Reports::getEOFMap
 * @param checkDate
 * @return
 */

QMap<int, QDateTime> Reports::getEOFMap(QDateTime checkDateTime)
{
    QDateTime last = Database::getLastReceiptDateTime();

    QMap<int, QDateTime> map;
    QDateTime lastEOD = getLastEODateTime();
    int type = getReportType();

    if (type == PAYED_BY_REPORT_EOM || type == PAYED_BY_MONTH_RECEIPT){
        type = PAYED_BY_REPORT_EOM;
        last = lastEOD;
    }

    if (type == PAYED_BY::FIRST_VALUE)
        return QMap<int, QDateTime>();

    // Tagesabschluss von Heute schon gemacht?
    if (type == PAYED_BY_REPORT_EOD && lastEOD.isValid() && lastEOD.secsTo(checkDateTime) <  0) {
        map.insert(PAYED_BY_REPORT_EOD, QDateTime());
        return map;
    }

    qint64 diff = getDiffTime(last);

    if (last.isValid() && type !=  PAYED_BY_REPORT_EOD && type != PAYED_BY_REPORT_EOM && last.addSecs(diff).secsTo(checkDateTime) > 0)
        map.insert(PAYED_BY_REPORT_EOD, last);

    diff -= 1;
    diff = diff - QTime(0,0,0).secsTo(Database::getCurfewTime());

    QString lastMonth = last.addSecs(diff).toString("yyyyMM");
    QString checkMonth = checkDateTime.addSecs(getDiffTime(checkDateTime)).toString("yyyyMM");

    // Monatsabschluss von diesen Monat schon gemacht?
    if (type == PAYED_BY_REPORT_EOM && lastMonth == checkMonth) {
        map.insert(PAYED_BY_REPORT_EOM, QDateTime());
        return map;
    }
    if (lastEOD.isValid() && lastEOD.date() > checkDateTime.date()) {
        map.insert(PAYED_BY_REPORT_EOM, QDateTime());
        return map;
    }

    if (last.isValid() && lastMonth != checkMonth && type !=  PAYED_BY_REPORT_EOM && last.addSecs(diff).secsTo(checkDateTime) > 0 /* && checkDateTime.date() != last.date()*/ )
        map.insert(PAYED_BY_REPORT_EOM, last);

    lastMonth = last.addMonths(1).toString("yyyyMM");
    if ((lastMonth < checkMonth) && (type ==  PAYED_BY_REPORT_EOM) && checkDateTime.date() != last.date())
        map.insert(PAYED_BY_REPORT_EOM, last.addMonths(1));

    return map;
}

bool Reports::mustDoEOAny(QDateTime check)
{
    QMap<int, QDateTime> map = getEOFMap(check);
    if (map.isEmpty())
        return false;

    qint64 diff = 0;
    if (map.contains(PAYED_BY_REPORT_EOD)) {
        QDateTime datetime = map.value(PAYED_BY_REPORT_EOD);
        if (datetime.isValid()) {
            diff = datetime.secsTo(check);
            if (diff > 86400)
                return !checkEOAny();
        }
    }
    if (map.contains(PAYED_BY_REPORT_EOM)) {
        QDateTime datetime = map.value(PAYED_BY_REPORT_EOM);
        if (datetime.isValid()) {
            diff = datetime.secsTo(check);
            if (diff > 86400)
                return !checkEOAny();
        }
    }

    return true;
}

/**
 * @brief Reports::checkEOAny
 * @param checkDate
 * @return
 */
bool Reports::checkEOAny(QDateTime check, bool checkDay)
{
    bool ret = true;
    QMap<int, QDateTime> map = getEOFMap(check);
    if (map.isEmpty())
        return true;

    if (map.contains(PAYED_BY_REPORT_EOD) && checkDay) {
        QDateTime datetime = map.value(PAYED_BY_REPORT_EOD);
        if (!datetime.isValid()) {
            if (!m_servermode)
                checkEOAnyMessageBoxInfo(PAYED_BY_REPORT_EOD, getLastEODateTime(), tr("Ein aktueller Tagesabschluss ist bereits vorhanden."));
            return false;
        }
    }

    if (map.contains(PAYED_BY_REPORT_EOM)) {
        QDateTime datetime = map.value(PAYED_BY_REPORT_EOM);
        if (!datetime.isValid()) {
            if (!m_servermode)
                checkEOAnyMessageBoxInfo(PAYED_BY_REPORT_EOM, getLastEODateTime(), tr("Monatsabschluss %1 wurde bereits erstellt.").arg( QLocale().monthName(check.addSecs(-getDiffTime(check)).date().month())));
            return false;
        }
    }

    QMapIterator<int, QDateTime> i(map);
    while (ret && i.hasNext()) {
        i.next();

        if (!m_servermode)
            ret = checkEOAnyMessageBoxYesNo(i.key(), i.value());

        if(ret) {
            if (i.key() == PAYED_BY_REPORT_EOD && checkDay) {
                ret = endOfDay();
                if (m_servermode && ret)
                    Spread::Instance()->setImportInfo(tr("Tagesabschluss vom %1 wurde erstellt.").arg(i.value().toString()));
                else if (m_servermode)
                    Spread::Instance()->setImportInfo(tr("Tagesabschluss vom %1 konnte nicht erstellt werden.").arg(i.value().toString()));
            } else if (i.key() == PAYED_BY_REPORT_EOM) {
                ret = endOfMonth();
                if (!m_servermode && !ret && RKSignatureModule::isSignatureModuleSetDamaged()) {
                    Spread::Instance()->setProgressBarValue(-1);
                    QString text = tr("Ein Signaturpflichtiger Beleg konnte nicht erstellt werden. Signatureinheit ausgefallen.");
                    checkEOAnyMessageBoxInfo(PAYED_BY_REPORT_EOM, QDateTime::currentDateTime(), text);
                }
            }
        }
    }

    return ret;
}

//bool Reports::checkEOAnyServerMode()
//{
//    return checkEOAny();
//}

/**
 * @brief Reports::checkEOAnyMessageBoxYesNo
 * @param type
 * @param date
 * @param text
 * @return
 */
bool Reports::checkEOAnyMessageBoxYesNo(int type, QDateTime datetime, QString text)
{
    QString infoText;

    qint64 diff = getDiffTime(datetime) -1;
    diff = diff - QTime(0,0,0).secsTo(Database::getCurfewTime());

    if (type == PAYED_BY_REPORT_EOD) {
        infoText = tr("Tagesabschluss");
        if (text.isEmpty()) text = tr("Tagesabschluss vom %1 muß erstellt werden.").arg(datetime.addSecs(diff).date().toString());
    } else {
        infoText = tr("Monatsabschluss");
        if (text.isEmpty()) text = tr("Monatsabschluss für %1 muß erstellt werden.").arg(datetime.date().toString("MMMM yyyy"));
    }

    QMessageBox msgBox;
    msgBox.setWindowTitle(infoText);

    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText(text);
    msgBox.setStandardButtons(QMessageBox::Yes);
    msgBox.addButton(QMessageBox::No);
    msgBox.setButtonText(QMessageBox::Yes, tr("Erstellen"));
    msgBox.setButtonText(QMessageBox::No, tr("Abbrechen"));
    msgBox.setDefaultButton(QMessageBox::No);

    if(msgBox.exec() == QMessageBox::Yes)
        return true;

    return false;
}

/**
 * @brief Reports::checkEOAnyMessageBoxInfo
 * @param type
 * @param date
 * @param text
 */
void Reports::checkEOAnyMessageBoxInfo(int type, QDateTime datetime, QString text)
{
    QString infoText;
    if (type == PAYED_BY_REPORT_EOD) {
        infoText = tr("Tagesabschluss");
    } else {
        infoText = tr("Monatsabschluss");
    }

    QMessageBox msgBox;
    msgBox.setWindowTitle(infoText);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText(text);

    msgBox.setInformativeText(tr("Erstellungsdatum %1").arg(datetime.date().toString()));
    msgBox.setStandardButtons(QMessageBox::Yes);
    msgBox.setButtonText(QMessageBox::Yes, tr("OK"));
    msgBox.exec();
}
bool Reports::endOfDay() {
    return endOfDay(true);
}

/**
 * @brief Reports::endOfDay
 * @return
 */
bool Reports::endOfDay(bool ask)
{
//    QDateTime datetime = getOffsetEODTime(Database::getLastReceiptDateTime());
    QDateTime datetime = Database::getLastReceiptDateTime();
    qint64 diff = getDiffTime(datetime) -1;
    diff = diff - QTime(0,0,0).secsTo(Database::getCurfewTime());

    bool create = canCreateEOD(datetime.addSecs(diff));
    if (create) {
        if (m_servermode)
            return doEndOfDay(datetime);

        QString text;
        bool ok = true;
//        if (ask && datetime.addSecs(diff).date() == QDateTime::currentDateTime().addSecs(diff).date()) {
        if (ask && datetime.addSecs(getDiffTime(datetime) -1).date() == QDateTime::currentDateTime().addSecs(getDiffTime(QDateTime::currentDateTime()) -1).date()) {
            text = tr("Nach dem Erstellen des Tagesabschlusses ist eine Bonierung für den heutigen Tag nicht mehr möglich.");
            ok = checkEOAnyMessageBoxYesNo(PAYED_BY_REPORT_EOD, datetime, text);
        }

        if (ok) {
            QRKProgress progress;
            progress.setText(tr("Tagesabschluss wird erstellt."));
            progress.setWaitMode(true);
            progress.show();
            qApp->processEvents();

            return doEndOfDay(datetime);
        }
    } else {
        if (!m_servermode)
            checkEOAnyMessageBoxInfo(PAYED_BY_REPORT_EOD, datetime, tr("Ein aktueller Tagesabschluss ist bereits vorhanden."));
    }

    return false;
}

/**
 * @brief Reports::doEndOfDay
 * @param date
 * @return
 */
bool Reports::doEndOfDay(QDateTime datetime)
{

    {
        QSqlDatabase dbc = Database::database();
        if (dbc.driverName() == "QSQLITE") {
            QSqlQuery query(dbc);
            query.exec("PRAGMA wal_checkpoint;");
            query.next();
            qDebug() << "Function Name: " << Q_FUNC_INFO << "WAL Checkpoint: (busy:" << query.value(0).toString() << ") log: " << query.value(1).toString() << " checkpointed: " << query.value(2).toString();
        }
    }

    Spread::Instance()->setProgressBarValue(1);
    Backup::create();

    Database::updateGlobals("curfewTemp", Q_NULLPTR, Q_NULLPTR);
    QSqlDatabase dbc = Database::database();
    dbc.transaction();
    m_currentReceipt = createReceipts();
    bool ret = finishReceipts(PAYED_BY_REPORT_EOD, 0, true);
    if (ret) {
        if (createEOD(m_currentReceipt, datetime)) {
            dbc.commit();
            printDocument(m_currentReceipt, tr("Tagesabschluss"));
        } else {
            dbc.rollback();
            return false;
        }
    } else {
        dbc.rollback();
        return false;
    }

    return ret;
}

/**
 * @brief Reports::endOfMonth
 * @return
 */
bool Reports::endOfMonth()
{

    QDateTime rDateTime = Database::getLastReceiptDateTime();
    // QDate rDate = rDateTime.date(); // Database::getLastReceiptDate();
    int type = getReportType();

    if (type == PAYED_BY_REPORT_EOD) {
        rDateTime = getLastEODateTime().addDays(-1);
    }

    if (type == PAYED_BY_REPORT_EOM || type == PAYED_BY_MONTH_RECEIPT) {
        rDateTime = getLastEODateTime().addMonths(1);
    }

    int receiptMonth = (rDateTime.date().year() * 100) + rDateTime.date().month();
    QDate curr = QDateTime::currentDateTime().date();
    int currMonth = (curr.year() * 100) + curr.month();

    bool ok = (rDateTime.isValid() && receiptMonth <= currMonth);

    if (ok) {
        QDateTime checkdate = QDateTime::currentDateTime();

        if (checkdate.date().year() == rDateTime.date().year()) {
            checkdate.setDate(QDate::fromString(QString("%1-%2-1")
                                                .arg(rDateTime.date().year())
                                                .arg(rDateTime.date().month())
                                                , "yyyy-M-d")
                              .addMonths(1).addDays(-1));
        } else {
            checkdate.setDate(QDate::fromString(QString("%1-12-31")
                                                .arg(QDate::currentDate().year()),"yyyy-M-d")
                              .addYears(-1));

            if (rDateTime.date().month() != checkdate.date().month()) {
                QString stringDate= QString("%1-%2-%3").arg(curr.addYears(-1).year()).arg(rDateTime.date().month()).arg(rDateTime.date().daysInMonth());

                checkdate.setDate(QDate::fromString(stringDate,"yyyy-M-d"));
            }
        }
        checkdate.setTime(QTime::fromString("23:59:59"));

        bool canCreateEom = canCreateEOM(rDateTime);

        if (!(type == PAYED_BY_REPORT_EOM) && !(type == PAYED_BY_MONTH_RECEIPT)) {
            bool canCreateEod = canCreateEOD(rDateTime);

            if (rDateTime.date() <= checkdate.date() && canCreateEod) {
                bool doJob = true;
                if (!m_servermode)
                    doJob = checkEOAnyMessageBoxYesNo(PAYED_BY_REPORT_EOD, rDateTime,tr("Der Tagesabschlusses für %1 muß zuerst erstellt werden.").arg(rDateTime.toString()));

                if (doJob) {
                    if (! endOfDay())
                        return false;
                } else {
                    return false;
                }
            }
        }

        if (canCreateEom) {
            ok = true;
            if (m_servermode) {
                if (doEndOfMonth(checkdate)) {
                    rDateTime = rDateTime.addMonths(1);
                    receiptMonth = (rDateTime.date().year() * 100) + rDateTime.date().month();
                    if (rDateTime.isValid() && receiptMonth < currMonth)
                        ok = checkEOAny();
                } else {
                    ok = false;
                }
                return ok;
            }

            if (receiptMonth == currMonth) {
                QString text = tr("Nach dem Erstellen des Monatsabschlusses ist eine Bonierung für diesen Monat nicht mehr möglich.");
                ok = checkEOAnyMessageBoxYesNo(PAYED_BY_REPORT_EOM, rDateTime, text);
            }

            QRKProgress progress;
            progress.setText(tr("Monatsabschluss wird erstellt."));
            progress.setWaitMode(true);
            progress.show();
            qApp->processEvents();

            if (ok) {
                if (doEndOfMonth(checkdate)){
                    rDateTime = rDateTime.addMonths(1);
                    receiptMonth = (rDateTime.date().year() * 100) + rDateTime.date().month();
                    if (rDateTime.isValid() && receiptMonth < currMonth)
                        ok = checkEOAny();
                } else {
                    ok = false;
                    QString text = tr("Monatsabschluss '%1' konnte nicht erstellt werden.").arg(QLocale().monthName(checkdate.date().month()));
                    checkEOAnyMessageBoxInfo(PAYED_BY_REPORT_EOM, QDateTime::currentDateTime(), text);
                }
            }
        }
    } else {
        QDate next = QDateTime::currentDateTime().date();
        next.setDate(next.year(), next.addMonths(1).month(), 1);
        if (!m_servermode) {
            QString text = tr("Der Monatsabschluss für %1 wurde schon gemacht. Der nächste Abschluss kann erst ab %2 gemacht werden.").arg(QLocale().monthName(QDate::currentDate().month())).arg(next.toString());
            checkEOAnyMessageBoxInfo(PAYED_BY_REPORT_EOM, QDateTime::currentDateTime(), text);
        }
    }

    return ok;
}

/**
 * @brief Reports::doEndOfMonth
 * @param date
 * @return
 */
bool Reports::doEndOfMonth(QDateTime datetime)
{
    {
        QSqlDatabase dbc = Database::database();
        if (dbc.driverName() == "QSQLITE") {
            QSqlQuery query(dbc);
            query.exec("PRAGMA wal_checkpoint;");
            query.next();
            qDebug() << "Function Name: " << Q_FUNC_INFO << "WAL Checkpoint: (busy:" << query.value(0).toString() << ") log: " << query.value(1).toString() << " checkpointed: " << query.value(2).toString();
        }
    }

    Spread::Instance()->setProgressBarValue(1);
    bool ret = false;
    Backup::create();
    clear();

    Database::updateGlobals("curfewTemp", Q_NULLPTR, Q_NULLPTR);

    QSqlDatabase dbc = Database::database();
    dbc.transaction();
    m_currentReceipt =  createReceipts();
    ret = finishReceipts(PAYED_BY_REPORT_EOM, 0, true);
    if (ret) {
        if (createEOM(m_currentReceipt, datetime)) {
            if (nullReceipt(datetime.date())) {
                dbc.commit();
                printDocument(m_currentReceipt, tr("Monatsabschluss"));
                if (m_servermode)
                    Spread::Instance()->setImportInfo(tr("Monatsabschluss vom %1 wurde erstellt.").arg(datetime.toString()));
            } else {
                dbc.rollback();
                if (m_servermode) {
                    if (RKSignatureModule::isSignatureModuleSetDamaged())
                        Spread::Instance()->setImportInfo(tr("Ein Signaturpflichtiger Beleg konnte nicht erstellt werden. Signatureinheit ausgefallen."), true);
                }
                return false;
            }
        } else {
            ret = false;
        }
    }
    if (!ret) {
        dbc.rollback();
        if (m_servermode)
            Spread::Instance()->setImportInfo(tr("Monatsabschluss vom %1 konnte nicht erstellt werden.").arg(datetime.toString()), true);
    }

    return ret;
}

bool Reports::nullReceipt(QDate date)
{
    bool ret = true;
    if (RKSignatureModule::isDEPactive()) {
        if ((date.year() < QDate::currentDate().year() && date.month() == 12) || date.month() == 12) {
            ret = createNullReceipt(YEAR_RECEIPT, date.toString("yyyy"));
            if (!ret) {}
            if (RKSignatureModule::isSignatureModuleSetDamaged()) {
                QString text = (tr("Ein Signaturpflichtiger Beleg konnte nicht erstellt werden. Signatureinheit ausgefallen."));
                if (!m_servermode)
                    checkEOAnyMessageBoxInfo(PAYED_BY_REPORT_EOM, QDateTime::currentDateTime(), text);
            }
        } else {
            ret = createNullReceipt(MONTH_RECEIPT, date.toString("MMM yyyy"));
        }

        if (!ret)
            return false;

        int counter = -1;
        Export xport;
        bool exp = xport.createBackup(counter);
        if (!exp && counter < 1) {
            QString text = tr("Das automatische DEP-7 Backup konnte nicht durchgeführt werden.\nStellen Sie sicher das Ihr externes Medium zu Verfügung steht und sichern Sie das DEP-7 manuell.");
            if (m_servermode)
                Spread::Instance()->setImportInfo("INFO: " + text, true);
            else
                checkEOAnyMessageBoxInfo(PAYED_BY_REPORT_EOM, QDateTime::currentDateTime(), text);
        }
    }

    return ret;
}

/**
 * @brief Reports::createEOD
 * @param id
 * @param date
 */
bool Reports::createEOD(int id, QDateTime datetime)
{
    QDateTime from;
    QDateTime to;

    // ---------- DAY -----------------------------
    from = datetime.addSecs(getDiffTime(datetime, true)+1).addDays(-1);
    to = datetime.addSecs(getDiffTime(datetime));

    QStringList eod;
    eod.append(createStat(id, "Tagesumsatz", from, to));

    QString line = QString("Tagesbeleg\tTagesbeleg\t\t%1\t%2\t0,0\t0,0\t0,0\t0,0\t0,0\t%3")
            .arg(id)
            .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
            .arg(QLocale().toString(Utils::getYearlyTotal(from.date().year()),'f', 2));

    bool ret = insert(eod, id, from, to);

    Journal journal;
    journal.journalInsertLine("Beleg", line);

    Spread::Instance()->setProgressBarValue(100);

    return ret;
}

/**
 * @brief Reports::createEOM
 * @param id
 * @param date
 */
bool Reports::createEOM(int id, QDateTime datetime)
{

    QDateTime from;
    QDateTime to;

    // ---------- MONTH -----------------------------
    from.setDate(QDate::fromString(QString("%1-%2-01").arg(datetime.date().year()).arg(datetime.date().month()),"yyyy-M-d"));
    from = from.addSecs(getDiffTime(from, true));
    to = datetime;
    to = to.addSecs(getDiffTime(datetime));

    QStringList eod;
    eod.append(createStat(id, "Monatsumsatz", from, to));

    // ----------- YEAR ---------------------------
    {
        QString fromString = QString("%1-01-01").arg(datetime.date().year());
        QDateTime from;
        from.setDate(QDate::fromString(fromString, "yyyy-MM-dd"));
        to = datetime;
        to.setTime(QTime::fromString("23:59:59"));
        to = to.addSecs(getDiffTime(to));

        if (datetime.date().month() == 12) {
            eod.append(createYearStat(id, datetime.date()));
        }
    }

    // ----------------------------------------------

    QString line = QString("Monatsbeleg\tMonatsbeleg\t\t%1\t%2\t0,0\t0,0\t0,0\t0,0\t0,0\t%3")
            .arg(id)
            .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
            .arg(QLocale().toString(Utils::getYearlyTotal(datetime.date().year()), 'f', 2));

    bool ret = insert(eod, id, from, to);

    Journal journal;
    journal.journalInsertLine("Beleg", line);

    Spread::Instance()->setProgressBarValue(100);

    return ret;
}

QDateTime Reports::getLastEODateTime()
{
    QSqlDatabase dbc = Database::database();
    QSqlQuery query(dbc);
    query.prepare("SELECT max(timestamp) AS timestamp, curfew FROM reports");
    bool ok = query.exec();
    if (!ok) {
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Error: " << query.lastError().text();
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);
    }

    if (query.last()) {
        QDateTime dateTime = query.value("timestamp").toDateTime();
        QTime curfew = QTime::fromString(query.value("curfew").toString(), "hh:mm");
        qint64 diff = dateTime.time().secsTo(curfew.addSecs(-1));
        if (diff != 0)
            dateTime = dateTime.addSecs(diff);

        diff = QTime(0,0,0).secsTo(curfew);
        return dateTime.addSecs(-diff); // getOffsetEODTime(dateTime);
    }

    return QDateTime();
}

/**
* @brief Reports::canCreateEOD
* @param date
* @return
*/
bool Reports::canCreateEOD(QDateTime datetime)
{
    int type = getReportType();
//    if (type != PAYED_BY_CONTROL_RECEIPT && type >= PAYED_BY_REPORT_EOD)
    if (type == PAYED_BY_REPORT_EOD || type == PAYED_BY_REPORT_EOM || type >= PAYED_BY_MONTH_RECEIPT)
        return false;

    QDateTime f = datetime;
    QDateTime t = QDateTime::currentDateTime();
//    f.setDate(QDate::fromString(date.toString()));
//    t.setDate(QDate::fromString(date.toString()));
    f = f.addSecs(1);

    QSqlDatabase dbc = Database::database();
    QSqlQuery query(dbc);
    query.prepare("SELECT reports.timestamp FROM reports, receipts where reports.timestamp BETWEEN :fromDate AND :toDate AND receipts.payedBy > 2 AND reports.receiptNum=receipts.receiptNum ORDER BY receipts.timestamp DESC LIMIT 1");
    query.bindValue(":fromDate", f.toString(Qt::ISODate));
    query.bindValue(":toDate", t.toString(Qt::ISODate));

    qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);

    bool ok = query.exec();
    if (!ok) {
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Error: " << query.lastError().text();
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);
    }

    if (query.last()) {
//        datetime = query.value("timestamp").toDateTime();
        return false;
    }

    return true;
}

/**
 * @brief Reports::canCreateEOM
 * @param date
 * @return
 */
bool Reports::canCreateEOM(QDateTime datetime)
{
    QDateTime f = datetime;
    QDateTime t = QDateTime::currentDateTime();
//    f.setDate(QDate::fromString(date.toString()));
//    t.setDate(QDate::fromString(date.toString()));
    f = f.addSecs(1);

    QSqlDatabase dbc = Database::database();
    QSqlQuery query(dbc);
    query.prepare("SELECT reports.timestamp FROM reports, receipts where reports.timestamp BETWEEN :fromDate AND :toDate AND receipts.payedBy = 4 AND reports.receiptNum=receipts.receiptNum ORDER BY receipts.timestamp DESC LIMIT 1");
    query.bindValue(":fromDate", f.toString(Qt::ISODate));
    query.bindValue(":toDate", t.toString(Qt::ISODate));

    bool ok = query.exec();
    if (!ok) {
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Error: " << query.lastError().text();
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);
    }

    if (query.last()) {
//        datetime = query.value("timestamp").toDateTime();
        return false;
    }

    return true;
}

/**
 * @brief Reports::getReportType
 * @return
 */
int Reports::getReportType()
{
    QSqlDatabase dbc = Database::database();
    QSqlQuery query(dbc);
    query.prepare("select payedBy,receiptNum from receipts where id=(select max(id) from receipts);");
    bool ok = query.exec();
    if (!ok) {
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Error: " << query.lastError().text();
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);
    }

    if (query.last()) {
        if (query.value("payedBy").isNull())
            return PAYED_BY::FIRST_VALUE;
        return query.value("payedBy").toInt();
    }
    return PAYED_BY::FIRST_VALUE;
}

/**
 * @brief Reports::createStat
 * @param id
 * @param type
 * @param from
 * @param to
 * @return
 */
QStringList Reports::createStat(int id, QString type, QDateTime from, QDateTime to)
{
    QrkSettings settings;

//    if (to.toString("yyyyMMdd") == QDate::currentDate().toString("yyyyMMdd"))
//        to.setTime(QTime::currentTime());

    QSqlDatabase dbc = Database::database();
    QSqlQuery query(dbc);

    /* Anzahl verkaufter Artikel oder Leistungen */
//    query.prepare("SELECT sum(ROUND(orders.count,2)) as count FROM orders WHERE receiptId IN (SELECT id FROM receipts WHERE timestamp BETWEEN :fromDate AND :toDate AND payedBy <= 2)");
    query.prepare("SELECT sum(orders.count) as count FROM orders WHERE receiptId IN (SELECT id FROM receipts WHERE timestamp BETWEEN :fromDate AND :toDate AND payedBy BETWEEN 0 AND 2)");
    query.bindValue(":fromDate", from.toString(Qt::ISODate));
    query.bindValue(":toDate", to.toString(Qt::ISODate));
//      query.bindValue(":toDate", QDateTime::currentDateTime().toString(Qt::ISODate));

    bool ok = query.exec();
    if (!ok) {
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Error: " << query.lastError().text();
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);
    }

    query.next();

    //double sumProducts = query.value("count").toDouble();
    QBCMath sumProducts(query.value("count").toDouble());
    sumProducts.round(2);

    QStringList stat;
    stat.append("=A");
    stat.append(QString("Anzahl verkaufter Artikel oder Leistungen: %1").arg(sumProducts.toLocale()));

    /* Anzahl Zahlungen */
    query.prepare("SELECT count(id) as count_id FROM receipts WHERE timestamp BETWEEN :fromDate AND :toDate AND payedBy BETWEEN 0 AND 2 AND storno < 2");
    query.bindValue(":fromDate", from.toString(Qt::ISODate));
    query.bindValue(":toDate", to.toString(Qt::ISODate));
    ok = query.exec();
    if (!ok) {
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Error: " << query.lastError().text();
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);
    }

    query.next();

    stat.append(QString("Anzahl Zahlungen: %1").arg(query.value("count_id").toInt()));

    /* Anzahl Stornos */
    query.prepare("SELECT count(id) as count_id FROM receipts WHERE timestamp BETWEEN :fromDate AND :toDate AND storno = 2");
    query.bindValue(":fromDate", from.toString(Qt::ISODate));
    query.bindValue(":toDate", to.toString(Qt::ISODate));

    ok = query.exec();
    if (!ok) {
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Error: " << query.lastError().text();
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);
    }

    query.next();

    stat.append(QString("Anzahl Stornos: %1").arg(query.value("count_id").toInt()));
    stat.append("-");


    /* Umsätze Zahlungsmittel */
    query.prepare("SELECT actionTypes.actionText, receiptNum, gross from receipts LEFT JOIN actionTypes on receipts.payedBy=actionTypes.actionId WHERE receipts.timestamp between :fromDate AND :toDate AND receipts.payedBy BETWEEN 0 AND 2 ORDER BY receipts.payedBy");
//    query.prepare(Database::getSalesPerPaymentSQLQueryString());
    query.bindValue(":fromDate", from.toString(Qt::ISODate));
    query.bindValue(":toDate", to.toString(Qt::ISODate));

    ok = query.exec();
    if (!ok) {
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Error: " << query.lastError().text();
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);
    }

    /*
     * FIXME: We do this workaroud, while SUM and ROUND will
     * give use the false result. SQL ROUND/SUM give xx.98 from xx.985
     * should be xx.99
     */

    stat.append(tr("Umsätze nach Zahlungsmittel"));

    QMap<QString, double> zm;
    while (query.next()) {
        QString key = query.value("actionText").toString();
        int id = query.value("receiptNum").toInt();
        QBCMath total(query.value("gross").toString());
        total.round(2);

        QMap<int, double> mixed = Database::getGiven(id);
        if (mixed.size() > 1) {
            QString type = Database::getActionType(mixed.lastKey());
            QBCMath secondPay(mixed.last());
            secondPay.round(2);
            total -= secondPay;
            if ( zm.contains(type) ) {
                zm[type] += secondPay.toDouble();
            } else {
                zm[type] = secondPay.toDouble();
            }
        }

        if ( zm.contains(key) ) {
            zm[key] += total.toDouble();
        } else {
            zm[key] = total.toDouble();
        }
        qApp->processEvents();
    }

    QMap<QString, double>::iterator i;
    QBCMath totalsum = 0.0;

    for (i = zm.begin(); i != zm.end(); ++i) {
        QString key = i.key();
        QBCMath total(i.value());
        total.round(2);
        stat.append(QString("%1: %2").arg(key, total.toLocale()));
        totalsum += total;
    }
    totalsum.round(2);
    stat.append("-");
    stat.append(QString("Summe: %1").arg(totalsum.toLocale()));
    stat.append("-");

    /*
     * FIXME: We do this workaroud, while SUM and ROUND will
     * give use the false result. SQL ROUND/SUM give xx.98 from xx.985
     * should be xx.99
     */

    /* Umsätze Steuern */
    query.prepare("SELECT orders.tax, receipts.receiptNum, (orders.count * orders.gross) - (orders.count * orders.gross * orders.discount) / 100 as total from receipts LEFT JOIN orders on orders.receiptId=receipts.receiptNum WHERE receipts.timestamp between :fromDate AND :toDate AND receipts.payedBy BETWEEN 0 AND 2 ORDER BY orders.tax");
    query.bindValue(":fromDate", from.toString(Qt::ISODate));
    query.bindValue(":toDate", to.toString(Qt::ISODate));

    ok = query.exec();
    if (!ok) {
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Error: " << query.lastError().text();
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);
    }

    stat.append(tr("Umsätze nach Steuersätzen"));
    QMap<double, double> map;
    map.clear();
    while (query.next()) {
        QBCMath key(query.value("tax").toString());
        key.round(2);
        QBCMath total(query.value("total").toString());
        total.round(2);
        if ( map.contains(key.toDouble()) ) {
            map[key.toDouble()] += total.toDouble();
        } else {
            map[key.toDouble()] = total.toDouble();
        }
        qApp->processEvents();
    }

    totalsum = 0.0;
    QMap<double, double>::iterator j;
    for (j = map.begin(); j != map.end(); ++j) {
        QBCMath k(j.key());
        QBCMath v(j.value());
        k.round(2);
        v.round(2);
        stat.append(QString("%1%: %2")
                    .arg(Utils::getTaxString(k))
                    .arg(v.toLocale()));
        totalsum += v;
    }
    totalsum.round(2);
    stat.append("-");
    stat.append(QString("Summe: %1").arg(totalsum.toLocale()));
    stat.append("-");

    /* stats per users */
    stat.append(tr("Umsätze nach Benutzer"));
    int size;
    QMap<QString, QMap<QString, double> > user = Database::getSalesPerUser(from.toString(Qt::ISODate), to.toString(Qt::ISODate), size);
    QMap<QString, QMap<QString, double> >::iterator u;
    totalsum = 0.0;
    QString username, prename;
    for (u = user.begin(); u != user.end(); ++u) {
        username = u.key();
        if (username.isEmpty())
            username = tr("n/a");
        QMap<QString, double> zm2 = u.value();
        QMap<QString, double>::iterator i;
        for (i = zm2.begin(); i != zm2.end(); ++i) {
            QString key = i.key();
            QBCMath total(i.value());
            total.round(2);
            stat.append(QString("%1: %2: %3").arg(username == prename?"\u00A0":username, key, total.toLocale()));
            totalsum += total;
            prename = username;
        }
        totalsum.round(2);
    }

    totalsum.round(2);
    stat.append("-");
    stat.append(QString("Summe: %1").arg(totalsum.toLocale()));
    stat.append("-");

    /* Summe */
    query.prepare("SELECT sum(gross) as total FROM receipts WHERE timestamp BETWEEN :fromDate AND :toDate AND payedBy BETWEEN 0 AND 2");
    query.bindValue(":fromDate", from.toString(Qt::ISODate));
    query.bindValue(":toDate", to.toString(Qt::ISODate));
    ok = query.exec();
    if (!ok) {
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Error: " << query.lastError().text();
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);
    }

    query.next();

    QBCMath totalgross(query.value("total").toDouble());
    totalgross.round(2);
    QString sales = totalgross.toLocale();

    if (type == "Jahresumsatz") {
        m_yearsales = sales;
    } else {
        //        qint64 diff = QTime(0,0,0).secsTo(query.value("curfew").toTime());
        qint64 diff = QTime(0,0,0).secsTo(Database::getCurfewTime());

        query.prepare("UPDATE receipts SET gross=:gross, timestamp=:timestamp, infodate=:infodate WHERE receiptNum=:receiptNum");
        query.bindValue(":gross", totalgross.toDouble());
        query.bindValue(":timestamp", QDateTime::currentDateTime().toString(Qt::ISODate));

        query.bindValue(":infodate", to.addSecs(-diff).toString(Qt::ISODate));
        query.bindValue(":receiptNum", id);

        ok = query.exec();

        if (!ok) {
            qWarning() << "Function Name: " << Q_FUNC_INFO << " Error: " << query.lastError().text();
            qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);
        }
    }

    stat.append(QString("%1: %2").arg(type).arg(sales));
    stat.append("=");

    /* Warengruppe
         * SELECT sum(orders.count) AS count, groups.name, orders.tax, SUM((orders.count * orders.gross) - ((orders.count * orders.gross / 100) * orders.discount)) as total FROM orders inner join groups as groups LEFT JOIN products ON orders.product=products.id  LEFT JOIN receipts ON receipts.receiptNum=orders.receiptId WHERE products.groupid = groups.id AND receipts.payedBy < 3 GROUP BY groups.name, products.tax ORDER BY orders.tax, products.name ASC
         * TODO:
         */
    if (Database::isAnyValueFunctionAvailable())
        query.prepare("SELECT groups.name, ANY_VALUE(products.tax) as tax, SUM((orders.count * orders.gross) - ((orders.count * orders.gross / 100) * orders.discount)) as total FROM orders inner join groups as groups LEFT JOIN products ON orders.product=products.id LEFT JOIN receipts ON receipts.receiptNum=orders.receiptId WHERE receipts.timestamp BETWEEN :fromDate AND :toDate AND products.groupid = groups.id AND receipts.payedBy < 3 GROUP BY groups.name, products.tax ORDER BY groups.name, products.tax ASC");
    else
        query.prepare("SELECT groups.name, products.tax as tax, SUM((orders.count * orders.gross) - ((orders.count * orders.gross / 100) * orders.discount)) as total FROM orders inner join groups as groups LEFT JOIN products ON orders.product=products.id LEFT JOIN receipts ON receipts.receiptNum=orders.receiptId WHERE receipts.timestamp BETWEEN :fromDate AND :toDate AND products.groupid = groups.id AND receipts.payedBy < 3 GROUP BY groups.name, products.tax ORDER BY groups.name, products.tax ASC");

    query.bindValue(":fromDate", from.toString(Qt::ISODate));
    query.bindValue(":toDate", to.toString(Qt::ISODate));

    ok = query.exec();
    if (!ok) {
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Error: " << query.lastError().text();
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);
    }

    stat.append("=W");
    stat.append(tr("Warengruppen Abrechnung"));
    stat.append("-");
    QBCMath total_productgroup(0);
    QString previousName = "";
    while (query.next()) {
        QBCMath total(query.value("total").toDouble());
        total.round(2);
        total_productgroup += total;
        QBCMath tax(query.value("tax").toDouble());
        tax.round(2);
        QBCMath totalTax;
        totalTax = total / (tax + 100.00) * 100.00;
        totalTax.round(2);
        totalTax = total - totalTax;
        totalTax.round(2);

        QString name = query.value("name").toString().trimmed();
        if (name.compare(previousName) != 0) {
            previousName = name;
            stat.append(QString("%1 : %2") /* do not remove the space before : */
                        .arg(query.value("name").toString())
                        .arg(total.toLocale()));
        }

        stat.append(tr("davon MwSt. %1%: %2")
                    .arg(Utils::getTaxString(tax).replace(".",","))
                    .arg(totalTax.toLocale()));

        qApp->processEvents();
    }
    total_productgroup.round(2);
    stat.append("-");
    stat.append(tr("Warengruppe Summe: %2").arg(total_productgroup.toLocale()));
    stat.append("=");

    // Artikelabrechnung
    if (Database::isAnyValueFunctionAvailable())
        query.prepare("SELECT groups.name as groupname, sum(orders.count) AS count, products.name, orders.gross, SUM((orders.count * orders.gross) - ((orders.count * orders.gross / 100) * orders.discount)) as total, ANY_VALUE(orders.tax) as tax, orders.discount FROM orders LEFT JOIN products ON orders.product=products.id LEFT JOIN groups ON products.groupid=groups.id LEFT JOIN receipts ON receipts.receiptNum=orders.receiptId WHERE receipts.timestamp BETWEEN :fromDate AND :toDate AND receipts.payedBy < 3 GROUP BY products.name, orders.gross, orders.discount ORDER BY groups.name, tax, products.name ASC");
    else
        query.prepare("SELECT groups.name as groupname, sum(orders.count) AS count, products.name, orders.gross, SUM((orders.count * orders.gross) - ((orders.count * orders.gross / 100) * orders.discount)) as total, orders.tax, orders.discount FROM orders LEFT JOIN products ON orders.product=products.id  LEFT JOIN groups ON products.groupid=groups.id LEFT JOIN receipts ON receipts.receiptNum=orders.receiptId WHERE receipts.timestamp BETWEEN :fromDate AND :toDate AND receipts.payedBy < 3 GROUP BY products.name, orders.gross, orders.discount ORDER BY groups.name, orders.tax, products.name ASC");

    query.bindValue(":fromDate", from.toString(Qt::ISODate));
    query.bindValue(":toDate", to.toString(Qt::ISODate));

    ok = query.exec();
    if (!ok) {
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Error: " << query.lastError().text();
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);
    }

    stat.append("=P");
    stat.append(tr("Verkaufte Artikel oder Leistungen (Gruppiert) Gesamt %1").arg(sumProducts.toLocale()));
    QString groupname = "";
    while (query.next()) {
        if (groupname.compare(query.value("groupname").toString()) != 0) {
            groupname = query.value("groupname").toString();
            stat.append(groupname);
        }
        QString name;
        if (query.value("discount").toDouble() != 0.0) {
            QBCMath discount(query.value("discount").toDouble());
            discount.round(2);
            name = QString("%1 (Rabatt -%2%)").arg(query.value("name").toString()).arg(discount.toLocale());
        } else {
            name = query.value("name").toString();
        }

        QBCMath total(query.value("total").toDouble());
        total.round(2);
        QBCMath gross(query.value("gross").toDouble());
        gross.round(2);
        QBCMath tax(query.value("tax").toDouble());
        tax.round(2);
        QBCMath count = query.value("count").toDouble();
        count.round(settings.value("decimalDigits", 2).toInt());

        stat.append(QString("%1: %2: %3: %4: %5%")
                    .arg(count.toLocale())
                    .arg(name)
                    .arg(gross.toLocale())
                    .arg(total.toLocale())
                    .arg(Utils::getTaxString(tax).replace(".",",")));

        qApp->processEvents();
    }
    stat.append("=");

    return stat;
}

/**
 * @brief Reports::insert
 * @param list
 * @param id
 */
bool Reports::insert(QStringList list, int id, QDateTime from, QDateTime to)
{

    QSqlDatabase dbc = Database::database();
    QSqlQuery query(dbc);

    int count = list.count();
    Spread::Instance()->setProgressBarValue(0);

    int i=0;
    bool ret = true;
    Journal journal;
    int type = 0;
    foreach (QString line, list) {
        if (line.startsWith(tr("=A"))) { type = 0; continue; }
        if (line.startsWith(tr("=W"))) { type = 1; continue; }
        if (line.startsWith(tr("=P"))) { type = 2; continue; }

        journal.journalInsertLine("Textposition", line);
        query.prepare("INSERT INTO reports (receiptNum, timestamp, text, type, curfew, timestampfrom) VALUES(:receiptNum, :timestamp, :text, :type, :curfew, :from)");
        query.bindValue(":receiptNum", id);
        query.bindValue(":timestamp", to.toString(Qt::ISODate));
        query.bindValue(":from", from.toString(Qt::ISODate));
        query.bindValue(":text", line);
        query.bindValue(":type", type);
        query.bindValue(":curfew", Database::getCurfewTime().toString("hh:mm"));

        ret = query.exec();
        if (!ret) {
            qWarning() << "Function Name: " << Q_FUNC_INFO << " Error: " << query.lastError().text();
            qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);
            break;
        }

        Spread::Instance()->setProgressBarValue(int((float(i++) / float(count)) * 100));
        qApp->processEvents();
    }
    return ret;
}

/**
 * @brief Reports::createYearStat
 * @param id
 * @param date
 * @return
 */
QStringList Reports::createYearStat(int id, QDate date)
{
    QDateTime from;
    QDateTime to;

    // ----------- YEAR ---------------------------
    QStringList eoy;
    QString fromString = QString("%1-01-01").arg(date.year());
    from.setDate(QDate::fromString(fromString, "yyyy-MM-dd"));
    from.setTime(QTime(0,0,0));
    from = from.addSecs(getDiffTime(from));
    to.setDate(QDate::fromString(date.toString()));
    to.setTime(QTime::fromString("23:59:59"));
    to = to.addSecs(getDiffTime(to));

    eoy.append("=A");
    eoy.append(QString("Jahressummen %1 (%2 - %3) :").arg(date.year()).arg(QLocale().toString(from, "dd.MM.yyyy hh&#058;mm")).arg(QLocale().toString(to, "dd.MM.yyyy hh&#058;mm")));
    eoy.append("-");

    eoy.append(createStat(id, "Jahresumsatz", from, to));

    return eoy;
}

/**
 * @brief Reports::getReport
 * @param id
 * @param test
 * @return
 */
QString Reports::getReport(int id, bool report_by_productgroup, bool test)
{
    QSqlDatabase dbc = Database::database();
    QSqlQuery query(dbc);
    QrkSettings settings;

    query.prepare("SELECT receipts.payedBy, reports.timestamp, reports.curfew, reports.timestampfrom FROM receipts JOIN reports ON receipts.receiptNum=reports.receiptNum WHERE receipts.receiptNum=:id");

    if (test)
        query.prepare("SELECT receipts.payedBy, reports.timestamp, reports.curfew FROM receipts JOIN reports ON receipts.receiptNum=reports.receiptNum WHERE receipts.receiptNum=(SELECT min(receiptNum) FROM reports)");
    else
        query.bindValue(":id", id);

    bool ok = query.exec();
    if (!ok) {
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Error: " << query.lastError().text();
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);
    }

    query.next();

    int type = query.value("payedBy").toInt();
    QString format = (type == PAYED_BY_REPORT_EOM)? "MMMM yyyy": "dd MMMM yyyy";

    QString header;
    QString header2 = "";
    if (test) {
        header = QString("TESTDRUCK für SCHRIFTART");
    } else {
        QDateTime to = query.value("timestamp").toDateTime();
        qint64 diff = QTime(0,0,0).secsTo(query.value("curfew").toTime());
        to = to.addSecs(-diff);
        header = QString("BON # %1, %2 - %3").arg(id).arg(Database::getActionType(type)).arg(to.date().toString(format));
        QDateTime from = query.value("timestampfrom").toDateTime();
        if (from.isValid() && from.date().year() > 0)
            header2 = QString("(%1 - %2)").arg(QLocale().toString(from, "dd.MM hh:mm")).arg(QLocale().toString(query.value("timestamp").toDateTime(), "dd.MM hh:mm"));
    }

    int not_type = 0;
    bool reporttype = settings.value("report_by_productgroup", false).toBool();
    if (report_by_productgroup)
        reporttype = !reporttype;

    if (reporttype) {
        not_type = 2;
    } else {
        not_type = 1;
    }

    query.prepare("SELECT text, type FROM reports WHERE receiptNum=:id AND type != :not_type");

    if (test)
        query.prepare("SELECT text FROM reports WHERE receiptNum=(SELECT min(receiptNum) FROM reports) AND type != :not_type");
    else
        query.bindValue(":id", id);

    query.bindValue(":not_type", not_type);

    ok = query.exec();
    if (!ok) {
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Error: " << query.lastError().text();
        qWarning() << "Function Name: " << Q_FUNC_INFO << " Query: " << Database::getLastExecutedQuery(query);
    }

    QString text;

    text.append("<!DOCTYPE html><html><head>\n");
    text.append("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\">\n");
    text.append("</head><body>\n<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"100%\">\n");

    int x = 0;

    int span = 4;
    text.append(QString("<tr><th colspan=\"%1\">%2</th></tr>").arg(span).arg(header) );
    if (!header2.isEmpty())
        text.append(QString("<tr><th colspan=\"%1\"><small>%2</small></th></tr>").arg(span).arg(header2));

    text.append(QString("<tr><th colspan=\"%1\"></th></tr>").arg(span));

    while (query.next()) {
        span = (query.value("type").toInt() == 2)?6 :5;

        QString t = query.value("text").toString();
        x++;
        QString color = "";
        if (x % 2 == 1)
            color = "bgcolor='#F5F5F5'";
        else
            color = "bgcolor='#FFFFFF'";

        text.append("<tr>");

        QStringList list;

        if (t.indexOf('-') == 0 && t.size() == 1) {
            span -= 1;
            t.replace('-',"<hr>");
            text.append(QString("<td colspan=\"%1\" %2>%3</td>").arg(span).arg(color).arg(t));
        } else if (t.indexOf('=') == 0  && t.size() == 1) {
            span -= 1;
            t.replace('=',"<hr size=\"5\">");
            text.append(QString("<td colspan=\"%1\" %2>%3</td>").arg(span).arg(color).arg(t));
        } else if (t.indexOf(QRegularExpression("[0-9]{1,2}%:")) != -1){
            list = t.split(":");
            span = span - list.count();
            foreach (const QString &str, list) {
                if (test)
                    text.append(QString("<td nowrap align=\"right\" colspan=\"%1\" %2>%3</td>").arg(span).arg(color).arg("0,00"));
                else
                    text.append(QString("<td nowrap align=\"right\" colspan=\"%1\" %2>%3</td>").arg(span).arg(color).arg(str));
                span = 1;
            }
        } else if (t.indexOf(QRegularExpression("^-*\\d+:|^-*\\d+\\.\\d+:|^-*\\d+,\\d+:")) != -1) {
            list = t.split(": ",QString::SkipEmptyParts);
            span = span - list.count();
            int count = 0;

            QString align = "left";
            foreach (const QString &str, list) {
                align = (count != 1)?"right":"left";

                bool nowrap = Utils::isNumber(str);
                if (test && count > 1)
                    text.append(QString("<td align=\"%1\" colspan=\"%2\" %3>%4</td>").arg(align).arg(span).arg(color).arg("0,00"));
                else if (nowrap)
                    text.append(QString("<td align=\"%1\" colspan=\"%2\" %3><nobr>%4</nobr></td>").arg(align).arg(span).arg(color).arg(str));
                else
                    text.append(QString("<td align=\"%1\" colspan=\"%2\" %3>%4</td>").arg(align).arg(span).arg(color).arg(str));

                count++;
                span = 1;
            }
        } else {
            list = t.split(":",QString::SkipEmptyParts);
            span = span - list.count();
            int count = 0;

            QString align = "left";
            foreach (const QString &str, list) {
                if (count > 0) align="right";
                if (test && count > 0)
                    text.append(QString("<td align=\"%1\" colspan=\"%2\" %3>%4</td>").arg(align).arg(span).arg(color).arg("0,00"));
                else
                    text.append(QString("<td align=\"%1\" colspan=\"%2\" %3>%4</td>").arg(align).arg(span).arg(color).arg(str));

                count++;
                span = 1;
            }
        }
        text.append("</tr>");
        qApp->processEvents();
    }
    text.append("</table></body></html>\n");

    return text;
}

void Reports::printDocument(int id, QString title)
{
    QString DocumentTitle = QString("BON_%1_%2").arg(id).arg(title);
    QTextDocument doc;
    doc.setHtml(Reports::getReport(id));

    if (RKSignatureModule::isDEPactive()) {
        QTextCursor cursor(&doc);
        cursor.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);
        bool isDamaged;
        QImage img = Utils::getQRCode(id, isDamaged).toImage();
        cursor.insertImage(img);
        if (isDamaged)
            cursor.insertHtml("</br><small>Sicherheitseinrichtung ausgefallen</small>");
    }

    DocumentPrinter p;
    p.printDocument(&doc, DocumentTitle);
}

qint64 Reports::getDiffTime(QDateTime dateTime, bool old)
{
    QTime time;
    if (old)
        time = Database::getLastEOACurfewTime();
    else
        time = Database::getCurfewTime();

    return getDiffTime(dateTime, time);
}

qint64 Reports::getDiffTime(QDateTime dateTime, QTime curfew)
{
    qint64 diff = 0;
    if (dateTime.time() > curfew) {
        diff = dateTime.time().secsTo(QTime(23,59,59));
        diff = diff + QTime(0,0,0).secsTo(curfew);
    } else {
        diff = dateTime.time().secsTo(curfew);
    }

    return diff;
}
