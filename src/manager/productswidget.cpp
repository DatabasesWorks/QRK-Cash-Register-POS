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

#include "productswidget.h"
#include "productedit.h"
#include "qrkdelegate.h"
#include "database.h"
#include "3rdparty/ckvsoft/qsqlrtmodel.h"
#include <ui_productswidget.h>

// #include <QSqlRelationalTableModel>
#include <QSqlRelation>
#include <QSqlQuery>
#include <QSortFilterProxyModel>
#include <QMessageBox>
#include <QHeaderView>
#include <QSqlRelationalDelegate>

//--------------------------------------------------------------------------------

ProductsWidget::ProductsWidget(QWidget *parent)
  : QWidget(parent), ui(new Ui::ProductsWidget), m_newProductDialog(Q_NULLPTR)
{
  ui->setupUi(this);

  connect(ui->plus, &QPushButton::clicked, this, &ProductsWidget::plusSlot);
  connect(ui->minus, &QPushButton::clicked, this, &ProductsWidget::minusSlot);
  connect(ui->edit, &QPushButton::clicked, this, &ProductsWidget::editSlot);
  connect(ui->productFilter, &QLineEdit::textChanged, this, &ProductsWidget::filterProduct);

  QSqlDatabase dbc = Database::database();

//  m_model = new QSqlRelationalTableModel(this, dbc);
  m_model = new QSqlRTModel(this, dbc);
  m_model->setTable("products");
  int groupidFieldIndex = m_model->fieldIndex("groupid");
  m_model->setRelation(groupidFieldIndex, QSqlRelation("groups", "id", "name"));

  m_model->setFilter("groupid > 1");
  m_model->setFrom("from (select max(version) as version, origin from products group by origin) p1 inner join (select * from products) as products on p1.version=products.version and p1.origin=products.origin");

  m_model->setEditStrategy(QSqlTableModel::OnFieldChange);
//  m_model->setEditStrategy(QSqlTableModel::OnRowChange);
  m_model->select();
  m_model->fetchMore();  // else the list is not filled with all possible rows

  m_model->setHeaderData(m_model->fieldIndex("itemnum"), Qt::Horizontal, tr("Artikel #"), Qt::DisplayRole);
  m_model->setHeaderData(m_model->fieldIndex("name"), Qt::Horizontal, tr("Artikelname"), Qt::DisplayRole);
  m_model->setHeaderData(m_model->fieldIndex("gross"), Qt::Horizontal, tr("Preis"), Qt::DisplayRole);
  m_model->setHeaderData(groupidFieldIndex, Qt::Horizontal, tr("Gruppe"), Qt::DisplayRole);
  m_model->setHeaderData(m_model->fieldIndex("visible"), Qt::Horizontal, tr("sichtbar"), Qt::DisplayRole);
  m_model->setHeaderData(m_model->fieldIndex("stock"), Qt::Horizontal, tr("Lagerbestand"), Qt::DisplayRole);
  m_model->setHeaderData(m_model->fieldIndex("minstock"), Qt::Horizontal, tr("Mindestbestand"), Qt::DisplayRole);

  m_proxyModel = new QSortFilterProxyModel(this);
  m_proxyModel->setSourceModel(m_model);
  m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
  m_proxyModel->setFilterKeyColumn(m_model->fieldIndex("name"));

  ui->tableView->setModel(m_proxyModel);
//  ui->tableView->setItemDelegate(new QSqlRelationalDelegate(ui->tableView));

  ui->tableView->setSortingEnabled(true);
  ui->tableView->setColumnHidden(m_model->fieldIndex("id"), true);
  ui->tableView->setColumnHidden(m_model->fieldIndex("barcode"), true);
  ui->tableView->setColumnHidden(m_model->fieldIndex("sold"), true);
  ui->tableView->setColumnHidden(m_model->fieldIndex("net"), true);
  ui->tableView->setColumnHidden(m_model->fieldIndex("completer"), true);
  ui->tableView->setColumnHidden(m_model->fieldIndex("color"), true);
  ui->tableView->setColumnHidden(m_model->fieldIndex("printerid"), true);
  ui->tableView->setColumnHidden(m_model->fieldIndex("image"), true);
  ui->tableView->setColumnHidden(m_model->fieldIndex("tax"), true);
  ui->tableView->setColumnHidden(m_model->fieldIndex("gross"), true);
  ui->tableView->setColumnHidden(m_model->fieldIndex("coupon"), true);
  ui->tableView->setColumnHidden(m_model->fieldIndex("button"), true);
  ui->tableView->setColumnHidden(m_model->fieldIndex("description"), true);

  ui->tableView->setColumnHidden(m_model->fieldIndex("version"), true);
  ui->tableView->setColumnHidden(m_model->fieldIndex("origin"), true);
  ui->tableView->setColumnHidden(m_model->fieldIndex("lastchange"), true);
  ui->tableView->setColumnHidden(m_model->fieldIndex("sortorder"), true);


  ui->tableView->setAlternatingRowColors(true);
  ui->tableView->resizeColumnsToContents();
  ui->tableView->horizontalHeader()->setStretchLastSection(true);

  ui->tableView->verticalHeader()->setVisible(false);
}

ProductsWidget::~ProductsWidget()
{
    delete ui;
}

//--------------------------------------------------------------------------------

void ProductsWidget::filterProduct(const QString &filter)
{
  // show only matching items

  m_proxyModel->setFilterWildcard("*" + filter + "*");

  m_model->fetchMore();  // else the list is not filled with all possible rows
                       // e.g. when using mouse wheel it would fetch more items
                       // but on the WeTab we have no mouse
}

//--------------------------------------------------------------------------------

void ProductsWidget::plusSlot()
{
  // reuse the "new" dialog so that the next call has already the previous
  // settings defined; makes input of a lot of products of a given group simpler
  // if ( !newProductDialog )
    m_newProductDialog = new ProductEdit(this);

  m_newProductDialog->exec();

  m_model->select();
  m_model->fetchMore();  // else the list is not filled with all possible rows
}

//--------------------------------------------------------------------------------

void ProductsWidget::minusSlot()
{
  int row = m_proxyModel->mapToSource(ui->tableView->currentIndex()).row();
  if ( row == -1 )
    return;

  QMessageBox msgBox;
  msgBox.setIcon(QMessageBox::Question);
  msgBox.setWindowTitle(tr("Artikel löschen"));
  msgBox.setText(tr("Möchten sie den Artikel '%1' wirklich löschen ?").arg(m_model->data(m_model->index(row, 3)).toString()));
  msgBox.setStandardButtons(QMessageBox::Yes);
  msgBox.addButton(QMessageBox::No);
  msgBox.setButtonText(QMessageBox::Yes, tr("Löschen"));
  msgBox.setButtonText(QMessageBox::No, tr("Abbrechen"));
  msgBox.setDefaultButton(QMessageBox::No);

  if(msgBox.exec() == QMessageBox::No)
      return;

  // m_model->removeRow(row);
  /* Workaround, removeRow always delete the product
   * QT5 5.12
  */
  //  int id = m_model->data(m_model->index(row, 0)).toInt();

  if (!m_model->removeRow(row) /* Database::exists("orders", id, "product")*/) {

      QMessageBox msgBox;
      msgBox.setIcon(QMessageBox::Information);
      msgBox.setWindowTitle(tr("Löschen nicht möglich"));
      msgBox.setText(tr("Artikel '%1' kann nicht gelöscht werden, da er schon in Verwendung ist.").arg(m_model->data(m_model->index(row, 3)).toString()));
      msgBox.setStandardButtons(QMessageBox::Yes);
      msgBox.setButtonText(QMessageBox::Yes, tr("Ok"));
      msgBox.exec();
      return;
  }

/*
  QSqlDatabase dbc = Database::database();
  QSqlQuery query(dbc);

  query.prepare("DELETE FROM products WHERE id=:id");
  query.bindValue(":id", id);

  query.exec();
*/
  m_model->select();
  m_model->fetchMore();  // else the list is not filled with all possible rows

}

//--------------------------------------------------------------------------------

void ProductsWidget::editSlot()
{
  QModelIndex current(m_proxyModel->mapToSource(ui->tableView->currentIndex()));
  int row = current.row();
  if ( row == -1 )
    return;

  ProductEdit dialog(this, m_model->data(m_model->index(row, m_model->fieldIndex("id"))).toInt());
  if ( dialog.exec() == QDialog::Accepted)
  {
    m_model->select();
    m_model->fetchMore();  // else the list is not filled with all possible rows
    m_proxyModel->fetchMore(current);
    ui->tableView->resizeRowsToContents();
    ui->tableView->setCurrentIndex(m_proxyModel->mapFromSource(current));
  }
}

//--------------------------------------------------------------------------------
