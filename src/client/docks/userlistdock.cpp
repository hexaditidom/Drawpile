/*
   DrawPile - a collaborative drawing program.

   Copyright (C) 2007-2013 Calle Laakkonen

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include <QListView>
#include <QPainter>
#include <QMouseEvent>

#include "docks/userlistdock.h"
#include "net/userlist.h"
#include "net/client.h"
#include "utils/icons.h"

#include "ui_userbox.h"

namespace widgets {

UserList::UserList(QWidget *parent)
	:QDockWidget(tr("Users"), parent)
{
	_ui = new Ui_UserBox;
	QWidget *w = new QWidget(this);
	setWidget(w);
	_ui->setupUi(w);
	setOperatorMode(false);

	_ui->userlist->setSelectionMode(QListView::SingleSelection);
	_ui->lockButton->setIcon(icon::lock());

	connect(_ui->lockButton, SIGNAL(clicked()), this, SLOT(lockSelected()));
	connect(_ui->kickButton, SIGNAL(clicked()), this, SLOT(kickSelected()));
}

void UserList::setOperatorMode(bool op)
{
	_ui->lockButton->setEnabled(op);
	_ui->kickButton->setEnabled(op);
}

void UserList::setClient(net::Client *client)
{
	_client = client;
	_ui->userlist->setModel(client->userlist());
	_ui->userlist->setItemDelegate(new UserListDelegate(this));

	connect(client->userlist(), SIGNAL(dataChanged(QModelIndex,QModelIndex)), this, SLOT(dataChanged(QModelIndex,QModelIndex)));
	connect(_ui->userlist->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)), this, SLOT(selectionChanged(QItemSelection)));
}

QModelIndex UserList::currentSelection()
{
	QModelIndexList sel = _ui->userlist->selectionModel()->selectedIndexes();
	if(sel.isEmpty())
		return QModelIndex();
	return sel.first();
}

void UserList::lockSelected()
{
	QModelIndex idx = currentSelection();
	if(idx.isValid())
		_client->sendLockUser(idx.data().value<net::User>().id, _ui->lockButton->isChecked());
}

void UserList::kickSelected()
{
	QModelIndex idx = currentSelection();
	if(idx.isValid())
		_client->sendKickUser(idx.data().value<net::User>().id);
}

void UserList::selectionChanged(const QItemSelection &selected)
{
	bool on = selected.count() > 0;
	setOperatorMode(on && _client->isOperator() && _client->isLoggedIn());

	if(on) {
		QModelIndex cs = currentSelection();
		dataChanged(cs,cs);
	}
}

void UserList::dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
	const int myRow = currentSelection().row();
	if(topLeft.row() <= myRow && myRow <= bottomRight.row()) {
		const net::User &user = currentSelection().data().value<net::User>();
		_ui->lockButton->setChecked(user.isLocked);
		if(user.isLocal)
			_ui->kickButton->setEnabled(false);
	}
}

UserListDelegate::UserListDelegate(QObject *parent)
	: QItemDelegate(parent)
{
}

void UserListDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	QStyleOptionViewItem opt = setOptions(index, option);
	painter->save();

	const net::User user = index.data().value<net::User>();

	// Background
	drawBackground(painter, opt, index);

	// Name
	QRect textrect = opt.rect;
	const QSize locksize = icon::lock().actualSize(QSize(16,16));

	if(user.isLocal)
		opt.font.setStyle(QFont::StyleItalic);

	if(user.isOperator)
		opt.palette.setColor(QPalette::Text, Qt::red);

	drawDisplay(painter, opt, textrect, user.name);

	// Lock indicator
	if(user.isLocked)
		painter->drawPixmap(
			opt.rect.topRight()-QPoint(locksize.width(), -opt.rect.height()/2+locksize.height()/2),
			icon::lock().pixmap(16, QIcon::Normal, QIcon::On)
		);

	painter->restore();
}

QSize UserListDelegate::sizeHint(const QStyleOptionViewItem & option, const QModelIndex & index ) const
{
	QSize size = QItemDelegate::sizeHint(option, index);
	const QSize iconsize = icon::lock().actualSize(QSize(16,16));
	if(size.height() < iconsize.height())
		size.setHeight(iconsize.height());
	return size;
}

}
