/*
 * This file is part of QRK - Qt Registrier Kasse
 *
 * Copyright (C) 2015-2018 Christian Kvasny <chris@ckvsoft.at>
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

#include "journal.h"
#include "defines.h"
#include "database.h"
#include "preferences/qrksettings.h"
#include "3rdparty/ckvsoft/rbac/acl.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QFile>
#include <QDebug>

Journal::Journal(QObject *parent)
  : QObject(parent)
{
}

void Journal::journalInsertReceipt(QJsonObject &data)
{
  QSqlDatabase dbc = Database::database();
  QSqlQuery query(dbc);

  QrkSettings settings;
  int digits = settings.value("decimalDigits", 2).toInt();;

  // Id Programmversion Kassen-Id Beleg Belegtyp Bemerkung Nachbonierung
  // Belegnummer Datum Umsatz_Normal Umsatz_Ermaessigt1 Umsatz_Ermaessigt2
  // Umsatz_Null Umsatz_Besonders Jahresumsatz_bisher Erstellungsdatum

  QString var;
  QString val = "(version,cashregisterid,datetime,text,userId)";

  QJsonArray a = data.value("Orders").toArray();

  foreach (const QJsonValue & value, a) {
    var.clear();
    QJsonObject o = value.toObject();
    var.append(QString("Produktposition\t"));
    QString itemNum = o["itemNum"].toString();
    if (!itemNum.isEmpty())
        itemNum = QString("%1 ").arg(itemNum);

    if (o["discount"].toDouble() != 0.0)
        var.append(QString("%1%2 Rabatt: -%3%\t").arg(itemNum).arg(o["product"].toString()).arg(QString::number(o["discount"].toDouble(),'f', 2)));
    else
        var.append(QString("%1 %2\t").arg(itemNum).arg(o["product"].toString()));
    var.append(QString("%1\t").arg(QString::number(o["count"].toDouble(),'f', digits)));
    var.append(QString("%1\t").arg(QString::number(o["singleprice"].toDouble(),'f', 2)));
    var.append(QString("%1\t").arg(QString::number(o["gross"].toDouble() - (o["gross"].toDouble() / 100) * o["discount"].toDouble(),'f', 2)));
    var.append(QString("%1\t").arg(o["tax"].toDouble()));
    var.append(QString("%1").arg(data.value("receiptTime").toString()));

    bool ok = query.prepare(QString("INSERT INTO journal %1 VALUES(:version,:kasse,:receiptTime,:var,:userId)")
        .arg(val));

    if (!ok) {
      qCritical() << "Function Name: " << Q_FUNC_INFO << " " << query.lastError().text();
      qCritical() << "Function Name: " << Q_FUNC_INFO << " " << Database::getLastExecutedQuery(query);
    }

    query.bindValue(":version", data.value("version").toString());
    query.bindValue(":kasse", data.value("kasse").toString());
    query.bindValue(":receiptTime", data.value("receiptTime").toString());
    query.bindValue(":var", var);
    query.bindValue(":userId", RBAC::Instance()->getUserId());

    if (!ok) {
      qCritical() << "Function Name: " << Q_FUNC_INFO << " " << query.lastError().text();
      qCritical() << "Function Name: " << Q_FUNC_INFO << " " << Database::getLastExecutedQuery(query);
    }

    query.exec();

  }

  var.clear();
  var.append(QString("%1\t").arg(data.value("actionText").toString()));
  var.append(QString("%1\t").arg(data.value("typeText").toString()));
  var.append(QString("%1\t").arg(data.value("comment").toString()));
  var.append(QString("%1\t").arg(data.value("totallyup").toString()));
  var.append(QString("%1\t").arg(data.value("receiptNum").toInt()));
  var.append(QString("%1\t").arg(data.value("receiptTime").toString()));
  var.append(QString("%1\t").arg(QString::number(data.value("Satz-Normal").toDouble(),'f',2)));
  var.append(QString("%1\t").arg(QString::number(data.value("Satz-Ermaessigt-1").toDouble(),'f',2)));
  var.append(QString("%1\t").arg(QString::number(data.value("Satz-Ermaessigt-2").toDouble(),'f',2)));
  var.append(QString("%1\t").arg(QString::number(data.value("Satz-Null").toDouble(),'f',2)));
  var.append(QString("%1\t").arg(QString::number(data.value("Satz-Besonders").toDouble(),'f',2)));
  var.append(QString("%1\t").arg(QString::number(data.value("sumYear").toDouble(),'f',2)));
  var.append(QString("%1").arg(data.value("receiptTime").toString()));

  bool ok = query.prepare(QString("INSERT INTO journal %1 VALUES(:version,:kasse,:date,:var,:userId)")
      .arg(val));

  if (!ok) {
    qCritical() << "Function Name: " << Q_FUNC_INFO << " " << query.lastError().text();
    qCritical() << "Function Name: " << Q_FUNC_INFO << " " << Database::getLastExecutedQuery(query);
  }

  query.bindValue(":version", data.value("version").toString());
  query.bindValue(":kasse", data.value("kasse").toString());
  query.bindValue(":date", QDateTime::currentDateTime());
  query.bindValue(":var", var);
  query.bindValue(":userId", RBAC::Instance()->getUserId());

  ok = query.exec();
  if (!ok) {
    qCritical() << "Function Name: " << Q_FUNC_INFO << " " << query.lastError().text();
    qCritical() << "Function Name: " << Q_FUNC_INFO << " " << Database::getLastExecutedQuery(query);
  }
}

void Journal::journalInsertLine(QString title,  QString text)
{

  QDateTime dt = QDateTime::currentDateTime();
  QSqlDatabase dbc = Database::database();
  QSqlQuery query(dbc);
  QString val = "(version,cashregisterid,datetime,text,userId)";
  bool ok = query.prepare(QString("INSERT INTO journal %1 VALUES(:version, :kasse, :date, :text, :userId)")
                      .arg(val));

  if (!ok) {
    qCritical() << "Function Name: " << Q_FUNC_INFO << " " << query.lastError().text();
    qCritical() << "Function Name: " << Q_FUNC_INFO << " " << Database::getLastExecutedQuery(query);
  }

  query.bindValue(":version", QString("%1.%2").arg(QRK_VERSION_MAJOR).arg(QRK_VERSION_MINOR));
  query.bindValue(":kasse", Database::getCashRegisterId());
  query.bindValue(":date", dt);
  query.bindValue(":text", title + "\t" + text + "\t" + dt.toString(Qt::ISODate));
  query.bindValue(":userId", RBAC::Instance()->getUserId());

  ok = query.exec();

  if (!ok) {
    qCritical() << "Function Name: " << Q_FUNC_INFO << " " << query.lastError().text();
    qCritical() << "Function Name: " << Q_FUNC_INFO << " " << Database::getLastExecutedQuery(query);
  }
}
