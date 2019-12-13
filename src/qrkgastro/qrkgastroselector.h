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

#ifndef QRKGASTROSELECTOR_H
#define QRKGASTROSELECTOR_H

#include <QWidget>

class DragPushButton;

namespace Ui {
class QRKGastroSelector;
}

class QRKGastroSelector : public QWidget
{
    Q_OBJECT

public:
    explicit QRKGastroSelector(QWidget *parent = Q_NULLPTR);
    ~QRKGastroSelector();

    DragPushButton *getTableButton(int id);
    void refresh();

signals:
    void cancelGastroButton_clicked(bool clicked = true);
    void tableOrder(int id);

private:
    Ui::QRKGastroSelector *ui;
    void manager();
    int getTableCount();

};

#endif // QRKGASTROSELECTOR_H