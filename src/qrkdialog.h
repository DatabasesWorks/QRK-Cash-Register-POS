/*
 * This file is part of QRK - Qt Registrier Kasse
 *
 * Copyright (C) 2015-2016 Christian Kvasny <chris@ckvsoft.at>
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
 */

#ifndef QRKDIALOG_H
#define QRKDIALOG_H

#include <QDialog>

class QLineEdit;

class QRKDialog : public QDialog
{
    Q_OBJECT
public:
    explicit QRKDialog(QWidget *parent = 0);

    void registerMandatoryField(QLineEdit* le, const QString& regexp = "");
    void unregisterMandatoryField(QLineEdit* le);

signals:
    void hasAcceptableInput(bool);

private slots:
    void checkLineEdits();

private:
    QList<QLineEdit*> _mandatoryFields;

};

#endif // QRKDIALOG_H