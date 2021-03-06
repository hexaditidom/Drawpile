/*
   DrawPile - a collaborative drawing program.

   Copyright (C) 2006-2013 Calle Laakkonen

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

#include <QDebug>
#include <QApplication>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QSettings>
#include <QFileDialog>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QUrl>
#include <QLabel>
#include <QMessageBox>
#include <QInputDialog>
#include <QCloseEvent>
#include <QPushButton>
#include <QImageReader>
#include <QSplitter>
#include <QClipboard>

#include "config.h"
#include "main.h"
#include "mainwindow.h"
#include "loader.h"

#include "canvasview.h"
#include "canvasscene.h"
#include "annotationitem.h"
#include "selectionitem.h"
#include "statetracker.h"
#include "toolsettings.h" // for setting annotation editor widgets Client pointer

#include "utils/recentfiles.h"
#include "utils/icons.h"
#include "utils/whatismyip.h"

#include "widgets/viewstatus.h"
#include "widgets/netstatus.h"
#include "widgets/dualcolorbutton.h"
#include "widgets/chatwidget.h"

#include "docks/toolsettingswidget.h"
#include "docks/palettebox.h"
#include "docks/navigator.h"
#include "docks/colorbox.h"
#include "docks/userlistdock.h"
#include "docks/layerlistdock.h"

#include "net/client.h"
#include "net/login.h"
#include "net/serverthread.h"

#include "dialogs/colordialog.h"
#include "dialogs/newdialog.h"
#include "dialogs/hostdialog.h"
#include "dialogs/joindialog.h"
#include "dialogs/settingsdialog.h"

MainWindow::MainWindow(bool restoreWindowPosition)
	: QMainWindow(), _canvas(0)
{
	updateTitle();

	initActions();
	createMenus();
	createToolbars();
	createDocks();

	QStatusBar *statusbar = new QStatusBar(this);
	setStatusBar(statusbar);

	// Create the view status widget
	widgets::ViewStatus *viewstatus = new widgets::ViewStatus(this);
	statusbar->addPermanentWidget(viewstatus);

	// Create net status widget
	widgets::NetStatus *netstatus = new widgets::NetStatus(this);
	statusbar->addPermanentWidget(netstatus);

	// Create lock status widget
	_lockstatus = new QLabel(this);
	_lockstatus->setPixmap(icon::lock().pixmap(16,QIcon::Normal,QIcon::Off));
	_lockstatus->setToolTip(tr("Board is not locked"));
	statusbar->addPermanentWidget(_lockstatus);

	// Work area is split between the canvas view and the chatbox
	splitter_ = new QSplitter(Qt::Vertical, this);
	setCentralWidget(splitter_);

	// Create canvas view
	_view = new widgets::CanvasView(this);
	_view->setToolSettings(_toolsettings);
	
	connect(_layerlist, SIGNAL(layerSelected(int)), _view, SLOT(selectLayer(int)));
	connect(_layerlist, SIGNAL(layerSelected(int)), this, SLOT(updateLockWidget()));

	splitter_->addWidget(_view);
	splitter_->setCollapsible(0, false);

	connect(toggleoutline_, SIGNAL(triggered(bool)),
			_view, SLOT(setOutline(bool)));
	connect(_toolsettings, SIGNAL(sizeChanged(int)),
			_view, SLOT(setOutlineRadius(int)));
	connect(_view, SIGNAL(imageDropped(QString)),
			this, SLOT(open(QString)));
	connect(_view, SIGNAL(viewTransformed(int, qreal)),
			viewstatus, SLOT(setTransformation(int, qreal)));

	connect(this, SIGNAL(toolChanged(tools::Type)), _view, SLOT(selectTool(tools::Type)));

	connect(_toolsettings->getColorPickerSettings(), SIGNAL(colorSelected(QColor)), fgbgcolor_, SLOT(setForeground(QColor)));
	
	// Create the chatbox
	widgets::ChatBox *chatbox = new widgets::ChatBox(this);
	splitter_->addWidget(chatbox);

	// Make sure the canvas gets the majority share of the splitter the first time
	splitter_->setStretchFactor(0, 1);
	splitter_->setStretchFactor(1, 0);

	// Create canvas scene
	_canvas = new drawingboard::CanvasScene(this);
	_canvas->setBackgroundBrush(
			palette().brush(QPalette::Active,QPalette::Window));
	_view->setCanvas(_canvas);
	navigator_->setScene(_canvas);

	connect(_canvas, SIGNAL(colorPicked(QColor)), fgbgcolor_, SLOT(setForeground(QColor)));
	connect(_canvas, SIGNAL(colorPicked(QColor)), _toolsettings->getColorPickerSettings(), SLOT(addColor(QColor)));
	connect(_canvas, &drawingboard::CanvasScene::myAnnotationCreated, _toolsettings->getAnnotationSettings(), &tools::AnnotationSettings::setSelection);
	connect(_canvas, SIGNAL(myLayerCreated(int)), _layerlist, SLOT(selectLayer(int)));
	connect(_canvas, SIGNAL(annotationDeleted(int)), _toolsettings->getAnnotationSettings(), SLOT(unselect(int)));
	connect(_canvas, &drawingboard::CanvasScene::canvasModified, [this]() { setWindowModified(true); });

	// Navigator <-> View
	connect(navigator_, SIGNAL(focusMoved(const QPoint&)),
			_view, SLOT(scrollTo(const QPoint&)));
	connect(_view, SIGNAL(viewMovedTo(const QRectF&)), navigator_,
			SLOT(setViewFocus(const QRectF&)));
	// Navigator <-> Zoom In/Out
	connect(navigator_, SIGNAL(zoomIn()), this, SLOT(zoomin()));
	connect(navigator_, SIGNAL(zoomOut()), this, SLOT(zoomout()));

	// Create the network client
	_client = new net::Client(this);
	_view->setClient(_client);
	_layerlist->setClient(_client);
	_toolsettings->getAnnotationSettings()->setClient(_client);
	_toolsettings->getAnnotationSettings()->setLayerSelector(_layerlist);
	_userlist->setClient(_client);

	// Client command receive signals
	connect(_client, SIGNAL(drawingCommandReceived(protocol::MessagePtr)), _canvas, SLOT(handleDrawingCommand(protocol::MessagePtr)));
	connect(_client, SIGNAL(needSnapshot(bool)), _canvas, SLOT(sendSnapshot(bool)));
	connect(_canvas, SIGNAL(newSnapshot(QList<protocol::MessagePtr>)), _client, SLOT(sendSnapshot(QList<protocol::MessagePtr>)));

	// Meta commands
	connect(_client, SIGNAL(chatMessageReceived(QString,QString, bool)),
			chatbox, SLOT(receiveMessage(QString,QString, bool)));
	connect(chatbox, SIGNAL(message(QString)), _client, SLOT(sendChat(QString)));
	connect(_client, SIGNAL(sessionTitleChange(QString)), this, SLOT(setSessionTitle(QString)));
	connect(_client, SIGNAL(opPrivilegeChange(bool)), this, SLOT(setOperatorMode(bool)));
	connect(_client, SIGNAL(sessionConfChange(bool,bool)), this, SLOT(sessionConfChanged(bool,bool)));
	connect(_client, SIGNAL(lockBitsChanged()), this, SLOT(updateLockWidget()));

	// Operator commands
	connect(_lockSession, SIGNAL(triggered(bool)), _client, SLOT(sendLockSession(bool)));
	connect(_closeSession, SIGNAL(triggered(bool)), _client, SLOT(sendCloseSession(bool)));

	// Network status changes
	connect(_client, SIGNAL(serverConnected(QString, int)), this, SLOT(connecting()));
	connect(_client, SIGNAL(serverLoggedin(bool)), this, SLOT(loggedin(bool)));
	connect(_client, SIGNAL(serverDisconnected(QString)), this, SLOT(disconnected(QString)));

	connect(_client, SIGNAL(serverConnected(QString, int)), netstatus, SLOT(connectingToHost(QString, int)));
	connect(_client, SIGNAL(serverLoggedin(bool)), netstatus, SLOT(loggedIn()));
	connect(_client, SIGNAL(serverDisconnecting()), netstatus, SLOT(hostDisconnecting()));
	connect(_client, SIGNAL(serverDisconnected(QString)), netstatus, SLOT(hostDisconnected()));
	connect(_client, SIGNAL(expectingBytes(int)),netstatus, SLOT(expectBytes(int)));
	connect(_client, SIGNAL(bytesReceived(int)), netstatus, SLOT(bytesReceived(int)));
	connect(_client, SIGNAL(bytesSent(int)), netstatus, SLOT(bytesSent(int)));

	connect(_client, SIGNAL(userJoined(QString)), netstatus, SLOT(join(QString)));
	connect(_client, SIGNAL(userLeft(QString)), netstatus, SLOT(leave(QString)));

	connect(_client, SIGNAL(userJoined(QString)), chatbox, SLOT(userJoined(QString)));
	connect(_client, SIGNAL(userLeft(QString)), chatbox, SLOT(userParted(QString)));

	// Restore settings
	readSettings(restoreWindowPosition);
	
	// Show self
	show();
}

MainWindow::~MainWindow()
{
	// Make sure all child dialogs are closed
	foreach(QObject *obj, children()) {
		QDialog *child = qobject_cast<QDialog*>(obj);
		delete child;
	}
}

/**
 * @brief Initialize session state
 *
 * If the document in this window cannot be replaced, a new mainwindow is created.
 *
 * @return the MainWindow instance in which the document was loaded or 0 in case of error
 */
MainWindow *MainWindow::loadDocument(SessionLoader &loader)
{
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

	MainWindow *win;
	if(canReplace()) {
		win = this;
	} else {
		writeSettings();
		win = new MainWindow(false);
	}
	
	QList<protocol::MessagePtr> init = loader.loadInitCommands();

	if(init.isEmpty()) {
		QApplication::restoreOverrideCursor();
		if(win != this)
			delete win;
		showErrorMessage(tr("An error occured while trying to open image"), loader.errorMessage());
		return 0;
	}

	win->_canvas->initCanvas(win->_client);
	win->_layerlist->init();
	win->_client->init();
	
	// Set local history size limit. This must be at least as big as the initializer,
	// otherwise a new snapshot will always have to be generated when hosting a session.
	uint minsizelimit = 0;
	foreach(protocol::MessagePtr msg, init)
		minsizelimit += msg->length();
	minsizelimit *= 2;

	win->_canvas->statetracker()->setMaxHistorySize(qMax(1024*1024*10u, minsizelimit));
	win->_client->sendLocalInit(init);

	QApplication::restoreOverrideCursor();

	win->filename_ = loader.filename();
	win->setWindowModified(false);
	win->updateTitle();
	win->save_->setEnabled(true);
	win->saveas_->setEnabled(true);
	win->_copy->setEnabled(true);
	win->_copylayer->setEnabled(true);
	return win;
}

/**
 * This function is used to check if the current board can be replaced
 * or if a new window is needed to open other content.
 *
 * The window can be replaced when there are no unsaved changes AND the
 * there is no active network connection
 * @retval false if a new window needs to be created
 */
bool MainWindow::canReplace() const {
	return !(isWindowModified() || _client->isConnected());
}

/**
 * The file is added to the list of recent files and the menus on all open
 * mainwindows are updated.
 * @param file filename to add
 */
void MainWindow::addRecentFile(const QString& file)
{
	RecentFiles::addFile(file);
	foreach(QWidget *widget, QApplication::topLevelWidgets()) {
		MainWindow *win = qobject_cast<MainWindow*>(widget);
		if(win)
			RecentFiles::initMenu(win->recent_);
	}
}

/**
 * Set window title according to currently open file and session
 */
void MainWindow::updateTitle()
{
	QString name;
	if(filename_.isEmpty()) {
		name = tr("Untitled");
	} else {
		const QFileInfo info(filename_);
		name = info.baseName();
	}

	if(!_canvas || _canvas->title().isEmpty())
		setWindowTitle(tr("%1[*] - DrawPile").arg(name));
	else
		setWindowTitle(tr("%1[*] - %2 - DrawPile").arg(name).arg(_canvas->title()));
}

/**
 * Load customized shortcuts
 */
void MainWindow::loadShortcuts()
{
	QSettings& cfg = DrawPileApp::getSettings();
	cfg.beginGroup("settings/shortcuts");

	QList<QAction*> actions = findChildren<QAction*>();
	foreach(QAction *a, actions) {
		if(!a->objectName().isEmpty() && cfg.contains(a->objectName())) {
			a->setShortcut(cfg.value(a->objectName()).value<QKeySequence>());
		}
	}
}

/**
 * Reload shortcuts after they have been changed, for all open main windows
 */
void MainWindow::updateShortcuts()
{
	foreach(QWidget *widget, QApplication::topLevelWidgets()) {
		MainWindow *win = qobject_cast<MainWindow*>(widget);
		if(win) {
			// First reset to defaults
			foreach(QAction *a, win->customacts_)
				a->setShortcut(
						a->property("defaultshortcut").value<QKeySequence>()
						);
			// Then reload customized
			win->loadShortcuts();
		}
	}
}

/**
 * Read and apply mainwindow related settings.
 */
void MainWindow::readSettings(bool windowpos)
{
	QSettings& cfg = DrawPileApp::getSettings();
	cfg.beginGroup("window");

	// Restore previously used window size and position
	resize(cfg.value("size",QSize(800,600)).toSize());

	if(windowpos && cfg.contains("pos")) {
		const QPoint pos = cfg.value("pos").toPoint();
		if(qApp->desktop()->availableGeometry().contains(pos))
			move(pos);
	}

	bool maximize = cfg.value("maximized", false).toBool();
	if(maximize)
		setWindowState(Qt::WindowMaximized);

	// Restore dock, toolbar and view states
	if(cfg.contains("state")) {
		restoreState(cfg.value("state").toByteArray());
	}
	if(cfg.contains("viewstate")) {
		splitter_->restoreState(cfg.value("viewstate").toByteArray());
	}

	lastpath_ = cfg.value("lastpath").toString();

	cfg.endGroup();
	cfg.beginGroup("tools");
	// Remember last used tool
	int tool = cfg.value("tool", 0).toInt();
	QList<QAction*> actions = _drawingtools->actions();
	if(tool<0 || tool>=actions.count()) tool=0;
	actions[tool]->trigger();
	_toolsettings->setTool(tools::Type(tool));

	// Remember cursor settings
	toggleoutline_->setChecked(cfg.value("outline",true).toBool());
	_view->setOutline(toggleoutline_->isChecked());

	// Remember foreground and background colors
	fgbgcolor_->setForeground(QColor(cfg.value("foreground", "black").toString()));
	fgbgcolor_->setBackground(QColor(cfg.value("background", "white").toString()));

	cfg.endGroup();

	// Customize shortcuts
	loadShortcuts();

	// Remember recent files
	RecentFiles::initMenu(recent_);
}

/**
 * Write out settings
 */
void MainWindow::writeSettings()
{
	QSettings& cfg = DrawPileApp::getSettings();
	cfg.beginGroup("window");
	
	cfg.setValue("pos", normalGeometry().topLeft());
	cfg.setValue("size", normalGeometry().size());
	
	cfg.setValue("maximized", isMaximized());
	cfg.setValue("state", saveState());
	cfg.setValue("viewstate", splitter_->saveState());
	cfg.setValue("lastpath", lastpath_);

	cfg.endGroup();
	cfg.beginGroup("tools");
	const int tool = _drawingtools->actions().indexOf(_drawingtools->checkedAction());
	cfg.setValue("tool", tool);
	cfg.setValue("outline", toggleoutline_->isChecked());
	cfg.setValue("foreground",fgbgcolor_->foreground().name());
	cfg.setValue("background",fgbgcolor_->background().name());
}

/**
 * Confirm exit. A confirmation dialog is popped up if there are unsaved
 * changes or network connection is open.
 * @param event event info
 */
void MainWindow::closeEvent(QCloseEvent *event)
{
	if(canReplace() == false) {

		// First confirm disconnection
		if(_client->isLoggedIn()) {
			QMessageBox box(
				QMessageBox::Information,
				tr("Exit DrawPile"),
				tr("You are still connected to a drawing session."),
				QMessageBox::NoButton, this);

			const QPushButton *exitbtn = box.addButton(tr("Exit anyway"),
					QMessageBox::AcceptRole);
			box.addButton(tr("Cancel"),
					QMessageBox::RejectRole);

			box.exec();
			if(box.clickedButton() == exitbtn) {
				_client->disconnectFromServer();
			} else {
				event->ignore();
				return;
			}
		}

		// Then confirm unsaved changes
		if(isWindowModified()) {
			QMessageBox box(QMessageBox::Question, tr("Exit DrawPile"),
					tr("There are unsaved changes. Save them before exiting?"),
					QMessageBox::NoButton, this);
			const QPushButton *savebtn = box.addButton(tr("Save"),
					QMessageBox::AcceptRole);
			box.addButton(tr("Discard"),
					QMessageBox::DestructiveRole);
			const QPushButton *cancelbtn = box.addButton(tr("Cancel"),
					QMessageBox::RejectRole);

			box.exec();
			bool cancel = false;
			// Save and exit, or cancel exit if couldn't save.
			if(box.clickedButton() == savebtn)
				cancel = !save();

			// Cancel exit
			if(box.clickedButton() == cancelbtn || cancel) {
				event->ignore();
				return;
			}
		}
	}
	exit();
}

/**
 * Show the "new document" dialog
 */
void MainWindow::showNew()
{
	dialogs::NewDialog *dlg = new dialogs::NewDialog(this);
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	connect(dlg, SIGNAL(accepted(QSize, QColor)), this, SLOT(newDocument(QSize, QColor)));

	if (_canvas->hasImage())
		dlg->setSize(QSize(_canvas->width(), _canvas->height()));
	else
		dlg->setSize(QSize(800, 600));

	dlg->setBackground(fgbgcolor_->background());
	dlg->show();
}

/**
 * Initialize the board and set background color to the one
 * chosen in the dialog.
 * If the document is unsaved, create a new window.
 */
void MainWindow::newDocument(const QSize &size, const QColor &background)
{
	BlankCanvasLoader bcl(size, background);
	loadDocument(bcl);
}

/**
 * @param action
 */
void MainWindow::openRecent(QAction *action)
{
	action->setProperty("deletelater",true);
	open(action->property("filepath").toString());
}

/**
 * Open the selected file
 * @param file file to open
 * @pre file.isEmpty()!=false
 */
void MainWindow::open(const QString& file)
{
	ImageCanvasLoader icl(file);
	if(loadDocument(icl)) {
		addRecentFile(file);
	}
}

/**
 * Show a file selector dialog. If there are unsaved changes, open the file
 * in a new window.
 */
void MainWindow::open()
{
	// Get a list of supported formats
	QString formats = "*.ora *.dptxt ";
	foreach(QByteArray format, QImageReader::supportedImageFormats()) {
		formats += "*." + format + " ";
	}
	const QString filter = tr("Images (%1);;All files (*)").arg(formats);

	// Get the file name to open
	const QString file = QFileDialog::getOpenFileName(this,
			tr("Open image"), lastpath_, filter);

	// Open the file if it was selected
	if(file.isEmpty()==false) {
		const QFileInfo info(file);
		lastpath_ = info.absolutePath();

		open(file);
	}

}

/**
 * Allows the user three choices:
 * <ul>
 * <li>Cancel</li>
 * <li>Go ahead and flatten the image, then save<li>
 * <li>Save in OpenRaster format instead</li>
 * </ul>
 * If user chooces to save in OpenRaster, the suffix of file parameter is
 * altered.
 * @param file file name (may be altered)
 * @return true if file should be saved
 */
bool MainWindow::confirmFlatten(QString& file) const
{
	QMessageBox box(QMessageBox::Information, tr("Save image"),
			tr("The selected format does not support layers or annotations."),
			QMessageBox::Cancel);
	box.addButton(tr("Flatten"), QMessageBox::AcceptRole);
	QPushButton *saveora = box.addButton(tr("Save as OpenRaster"), QMessageBox::ActionRole);

	// Don't save at all
	if(box.exec() == QMessageBox::Cancel)
		return false;
	
	// Save
	if(box.clickedButton() == saveora) {
		file = file.left(file.lastIndexOf('.')) + ".ora";
	}
	return true;
}

/**
 * If no file name has been selected, \a saveas is called.
 */
bool MainWindow::save()
{
	if(filename_.isEmpty()) {
		return saveas();
	} else {
		if(QFileInfo(filename_).suffix() != "ora" && _canvas->needSaveOra()) {
			if(confirmFlatten(filename_)==false)
				return false;
		}
		QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
		bool saved = _canvas->save(filename_);
		QApplication::restoreOverrideCursor();
		if(!saved) {
			showErrorMessage(tr("Couldn't save image"));
			return false;
		} else {
			setWindowModified(false);
			addRecentFile(filename_);
			return true;
		}
	}
}

/**
 * A standard file dialog is used to get the name of the file to save.
 * If no suffix is the suffix from the current filter is used.
 */
bool MainWindow::saveas()
{
	QString selfilter;
	QString filter;
#if 0
	// Get a list of supported formats
	foreach(QByteArray format, QImageWriter::supportedImageFormats()) {
		filter += QString(format).toUpper() + " (*." + format + ");;";
	}
#endif
	// We build the filter manually, because these are pretty much the only
	// reasonable formats (who would want to save a 1600x1200 image
	// as an XPM?). Perhaps we should check GIF support was compiled in?
	filter = "OpenRaster (*.ora);;PNG (*.png);;JPEG (*.jpeg);;BMP (*.bmp);;";
	filter += tr("All files (*)");

	// Get the file name
	QString file = QFileDialog::getSaveFileName(this,
			tr("Save image"), lastpath_, filter, &selfilter);

	if(file.isEmpty()==false) {

		// Set file suffix if missing
		const QFileInfo info(file);
		if(info.suffix().isEmpty()) {
			if(selfilter.isEmpty()) {
				// If we don't have selfilter, pick what is best
				if(_canvas->needSaveOra())
					file += ".ora";
				else
					file += ".png";
			} else {
				// Use the currently selected filter
				int i = selfilter.indexOf("*.")+1;
				int i2 = selfilter.indexOf(')', i);
				file += selfilter.mid(i, i2-i);
			}
		}

		// Confirm format choice if saving would result in flattening layers
		if(_canvas->needSaveOra() && !file.endsWith(".ora", Qt::CaseInsensitive)) {
			if(confirmFlatten(file)==false)
				return false;
		}

		// Save the image
		QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
		bool saved = _canvas->save(file);
		QApplication::restoreOverrideCursor();
		if(!saved) {
			showErrorMessage(tr("Couldn't save image"));
			return false;
		} else {
			filename_ = file;
			setWindowModified(false);
			updateTitle();
			return true;
		}
	}
	return false;
}

/**
 * The settings window will be window modal and automatically destruct
 * when it is closed.
 */
void MainWindow::showSettings()
{
	dialogs::SettingsDialog *dlg = new dialogs::SettingsDialog(customacts_, this);
	connect(dlg, SIGNAL(shortcutsChanged()), this, SLOT(updateShortcuts()));
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setWindowModality(Qt::WindowModal);
	dlg->show();
}

void MainWindow::host()
{
	hostdlg_ = new dialogs::HostDialog(_canvas->image(), this);
	connect(hostdlg_, SIGNAL(finished(int)), this, SLOT(finishHost(int)));
	hostdlg_->show();
}

/**
 * Show the join dialog
 */
void MainWindow::join()
{
	joindlg_ = new dialogs::JoinDialog(this);
	connect(joindlg_, SIGNAL(finished(int)), this, SLOT(finishJoin(int)));
	joindlg_->show();
}

/**
 * Leave action triggered, ask for confirmation
 */
void MainWindow::leave()
{
	QMessageBox *leavebox = new QMessageBox(
		QMessageBox::Question,
		_canvas->title().isEmpty()?tr("Untitled session"):_canvas->title(),
		tr("Really leave the session?"),
		QMessageBox::NoButton,
		this,
		Qt::MSWindowsFixedSizeDialogHint|Qt::Sheet
	);
	leavebox->setAttribute(Qt::WA_DeleteOnClose);
	leavebox->addButton(tr("Leave"), QMessageBox::YesRole);
	leavebox->setDefaultButton(
			leavebox->addButton(tr("Stay"), QMessageBox::NoRole)
			);
	connect(leavebox, &QMessageBox::finished, [this](int result) {
		if(result == 0)
			_client->disconnectFromServer();
	});
	
	if(_client->uploadQueueBytes() > 0) {
		leavebox->setIcon(QMessageBox::Warning);
		leavebox->setInformativeText(tr("There is still unsent data! Please wait until transmission completes!"));
	}

	leavebox->show();
}

/**
 * User has finally decided to connect to a host (possibly localhost)
 * and host a session.
 * @param i dialog return value
 */
void MainWindow::finishHost(int i)
{
	if(i==QDialog::Accepted) {
		const bool useremote = hostdlg_->useRemoteAddress();
		QUrl address;

		if(useremote) {
			QString scheme;
			if(hostdlg_->getRemoteAddress().startsWith("drawpile://")==false)
				scheme = "drawpile://";
			address = QUrl(scheme + hostdlg_->getRemoteAddress(),
					QUrl::TolerantMode);
		} else {
			address.setHost(WhatIsMyIp::localAddress());
		}

		if(address.isValid() == false || address.host().isEmpty()) {
			hostdlg_->show();
			showErrorMessage(tr("Invalid address"));
			return;
		}
		address.setUserName(hostdlg_->getUserName());

		// Remember some settings
		hostdlg_->rememberSettings();

		// Start server if hosting locally
		if(useremote==false) {
			net::ServerThread *server = new net::ServerThread(this);

			QSettings &cfg = DrawPileApp::getSettings();
			if(cfg.contains("settings/server/port"))
				server->setPort(cfg.value("settings/server/port").toInt());

			int port = server->startServer();
			if(!port) {
				QMessageBox::warning(this, tr("Unable to start server"), tr("An error occurred while trying to start the server"));
				hostdlg_->show();
				delete server;
				return;
			}
			server->setDeleteOnExit();

			if(!server->isOnDefaultPort())
				address.setPort(port);
		}

		// Initialize session (unless original was used)
		MainWindow *w = this;
		if(hostdlg_->useOriginalImage() == false) {
			QScopedPointer<SessionLoader> loader(hostdlg_->getSessionLoader());
			w = loadDocument(*loader);
		}

		// Connect to server
		net::LoginHandler *login = new net::LoginHandler(net::LoginHandler::HOST, address);
		login->setPassword(hostdlg_->getPassword());
		login->setTitle(hostdlg_->getTitle());
		login->setMaxUsers(hostdlg_->getUserLimit());
		login->setAllowDrawing(hostdlg_->getAllowDrawing());
		w->_client->connectToServer(login);

	}
	hostdlg_->deleteLater();
}

/**
 * User has finally decided to connect to a server and join a session.
 * If there are multiple sessions, we will have to ask the user which
 * one to join later.
 */
void MainWindow::finishJoin(int i) {
	if(i==QDialog::Accepted) {
		QString scheme;
		if(joindlg_->getAddress().startsWith("drawpile://")==false)
			scheme = "drawpile://";
		QUrl address = QUrl(scheme + joindlg_->getAddress(),QUrl::TolerantMode);
		if(address.isValid()==false || address.host().isEmpty()) {
			joindlg_->show();
			showErrorMessage(tr("Invalid address"));
			return;
		}
		address.setUserName(joindlg_->getUserName());

		// Remember some settings
		joindlg_->rememberSettings();

		// Connect
		joinSession(address);
	}
	joindlg_->deleteLater();
}

void MainWindow::changeSessionTitle()
{
	bool ok;
	QString newtitle = QInputDialog::getText(
				this,
				tr("Session title"),
				tr("Change session title"),
				QLineEdit::Normal,
				_canvas->title(),
				&ok
	);
	if(ok && newtitle != _canvas->title()) {
		_client->sendSetSessionTitle(newtitle);
	}
}

/**
 * @param url URL
 */
void MainWindow::joinSession(const QUrl& url)
{
	MainWindow *win;
	if(canReplace())
		win = this;
	else
		win = new MainWindow(false);

	net::LoginHandler *login = new net::LoginHandler(net::LoginHandler::JOIN, url);
	win->_client->connectToServer(login);
}

/**
 * Now connecting to server
 */
void MainWindow::connecting()
{
	host_->setEnabled(false);
	logout_->setEnabled(true);

	// Disable UI until login completes
	_view->setEnabled(false);
	_drawingtools->setEnabled(false);
}

/**
 * Connection lost, so disable and enable some UI elements
 */
void MainWindow::disconnected(const QString &message)
{
	host_->setEnabled(true);
	logout_->setEnabled(false);
	adminTools_->setEnabled(false);

	// Re-enable UI
	_view->setEnabled(true);
	_drawingtools->setEnabled(true);

	setSessionTitle(QString());

	// This should be true at this time still
	if(!_client->isLoggedIn()) {
		showErrorMessage(tr("Couldn't connect to server"), message);
	}

	// Make sure all drawing is complete
	if(_canvas->hasImage())
		_canvas->statetracker()->endRemoteContexts();
}

/**
 * Server connection established and login successfull
 */
void MainWindow::loggedin(bool join)
{
	// Re-enable UI
	_view->setEnabled(true);
	_drawingtools->setEnabled(true);

	// Initialize the canvas (in host mode the canvas was prepared already)
	if(join) {
		_canvas->initCanvas(_client);
		_layerlist->init();
	}
}

void MainWindow::sessionConfChanged(bool locked, bool closed)
{
	_lockSession->setChecked(locked);
	_closeSession->setChecked(closed);
}

void MainWindow::updateLockWidget()
{
	bool locked = _client->isLocked() || _layerlist->isCurrentLayerLocked();
	if(locked) {
		_lockstatus->setPixmap(icon::lock().pixmap(16,QIcon::Normal,QIcon::On));
		_lockstatus->setToolTip(tr("Board is locked"));
	} else {
		_lockstatus->setPixmap(icon::lock().pixmap(16,QIcon::Normal,QIcon::Off));
		_lockstatus->setToolTip(tr("Board is not locked"));
	}
	_view->setLocked(locked);
}

void MainWindow::setForegroundColor()
{
	fgdialog_->setColor(fgbgcolor_->foreground());
	fgdialog_->show();
}

void MainWindow::setBackgroundColor()
{
	bgdialog_->setColor(fgbgcolor_->background());
	bgdialog_->show();
}

/**
 * Session title changed
 * @param title new title
 */
void MainWindow::setSessionTitle(const QString& title)
{
	_canvas->setTitle(title);
	updateTitle();
}

void MainWindow::setOperatorMode(bool op)
{
	// Don't enable these actions in local mode
	adminTools_->setEnabled(op && _client->isLoggedIn());
}

/**
 * Write settings and exit. The application will not be terminated until
 * the last mainwindow is closed.
 */
void MainWindow::exit()
{
	if(windowState().testFlag(Qt::WindowFullScreen))
		fullscreen(false);
	writeSettings();
	deleteLater();
}

/**
 * @param message error message
 * @param details error details
 */
void MainWindow::showErrorMessage(const QString& message, const QString& details)
{
	QMessageBox *msgbox = new QMessageBox(
		QMessageBox::Warning,
		QString("DrawPile"),
		message, QMessageBox::Ok,
		this,
		Qt::Dialog|Qt::Sheet|Qt::MSWindowsFixedSizeDialogHint
	);
	msgbox->setAttribute(Qt::WA_DeleteOnClose);
	msgbox->setWindowModality(Qt::WindowModal);
	msgbox->setDetailedText(details);
	msgbox->show();
}

/**
 * Increase zoom factor
 */
void MainWindow::zoomin()
{
	int nz = _view->zoom() * 2;
	if(nz>25) {
		// When zoom% is over 25, make sure we increase in nice evenly
		// dividing increments.
		if(nz % 25) nz = nz / 25 * 25;
	}

	_view->setZoom(nz);
}

/**
 * Decrease zoom factor
 */
void MainWindow::zoomout()
{
	_view->setZoom(_view->zoom() / 2);
}

/**
 * Set zoom factor to 100%
 */
void MainWindow::zoomone()
{
	_view->setZoom(100);
}

/**
 * Set rotation angle to 0
 */
void MainWindow::rotatezero()
{
	_view->setRotation(0.0);
}

void MainWindow::toggleAnnotations(bool hidden)
{
	annotationtool_->setEnabled(!hidden);
	_canvas->showAnnotations(!hidden);
	if(hidden) {
		if(annotationtool_->isChecked())
			brushtool_->trigger();
		// lasttool_ might be erasertool_ when tablet is brought near
		if(lasttool_ == annotationtool_)
			lasttool_ = brushtool_;
	}

}

/**
 * Toggle fullscreen mode for editor view
 */
void MainWindow::fullscreen(bool enable)
{
	static QByteArray oldstate;
	static QPoint oldpos;
	static QSize oldsize;
	if(enable) {
		Q_ASSERT(windowState().testFlag(Qt::WindowFullScreen)==false);
		// Save state
		oldstate = saveState();
		oldpos = pos();
		oldsize = size();
		// Hide everything except the central widget
		/** @todo hiding the menu bar disables shortcut keys */
		statusBar()->hide();
		const QObjectList c = children();
		foreach(QObject *child, c) {
			if(child->inherits("QToolBar") || child->inherits("QDockWidget"))
				(qobject_cast<QWidget*>(child))->hide();
		}
		showFullScreen();
	} else {
		Q_ASSERT(windowState().testFlag(Qt::WindowFullScreen)==true);
		// Restore old state
		showNormal();
		statusBar()->show();
		resize(oldsize);
		move(oldpos);
		restoreState(oldstate);
	}
}

/**
 * User selected a tool
 * @param tool action representing the tool
 */
void MainWindow::selectTool(QAction *tool)
{
	tools::Type type;
	if(tool == pentool_) 
		type = tools::PEN;
	else if(tool == brushtool_) 
		type = tools::BRUSH;
	else if(tool == erasertool_) 
		type = tools::ERASER;
	else if(tool == pickertool_) 
		type = tools::PICKER;
	else if(tool == linetool_) 
		type = tools::LINE;
	else if(tool == recttool_) 
		type = tools::RECTANGLE;
	else if(tool == annotationtool_)
		type = tools::ANNOTATION;
	else if(tool == selectiontool_)
		type = tools::SELECTION;
	else
		return;
	lasttool_ = tool;

	// When using the annotation tool, highlight all text boxes
	_canvas->showAnnotationBorders(type==tools::ANNOTATION);

	emit toolChanged(type);
}

/**
 * When the eraser is near, switch to eraser tool. When not, switch to
 * whatever tool we were using before
 * @param near
 */
void MainWindow::eraserNear(bool near)
{
	if(near) {
		QAction *lt = lasttool_; // Save lasttool_
		erasertool_->trigger();
		lasttool_ = lt;
	} else {
		lasttool_->trigger();
	}
}

void MainWindow::copyLayer()
{
	_canvas->copyToClipboard(_layerlist->currentLayer());
}

void MainWindow::copyVisible()
{
	_canvas->copyToClipboard(0);
}

void MainWindow::paste()
{
	selectiontool_->trigger();
	if(_canvas->hasImage()) {
		_canvas->pasteFromClipboard();
	} else {
		// Canvas not yet initialized? Initialize with clipboard content
		QImage image = QApplication::clipboard()->image();
		if(image.isNull())
			return;

		QImageCanvasLoader loader(image);
		loadDocument(loader);
	}
}

void MainWindow::about()
{
	QMessageBox::about(this, tr("About DrawPile"),
			tr("<p><b>DrawPile %1</b><br>"
			"A collaborative drawing program.</p>"
			"<p>This program is free software; you may redistribute it and/or "
			"modify it under the terms of the GNU General Public License as " 
			"published by the Free Software Foundation, either version 2, or "
			"(at your opinion) any later version.</p>"
			"<p>Programming: Calle Laakkonen, M.K.A<br>"
			"Icons are from the Tango Desktop Project</p>").arg(DRAWPILE_VERSION)
			);
}

void MainWindow::homepage()
{
	QDesktopServices::openUrl(QUrl("http://drawpile.sourceforge.net/"));
}

/**
 * A utility function for creating an editable action. All created actions
 * are added to a list that is used in the settings dialog to edit
 * the shortcuts.
 * @param name (internal) name of the action. If null, no name is set. If no name is set, the shortcut cannot be customized.
 * @param icon name of the icon file to use. If 0, no icon is set.
 * @param text action text
 * @param tip status bar tip
 * @param shortcut default shortcut
 */
QAction *MainWindow::makeAction(const char *name, const char *icon, const QString& text, const QString& tip, const QKeySequence& shortcut)
{
	QAction *act;
	QIcon qicon;
	if(icon)
		qicon = QIcon(QString(":icons/") + icon);
	act = new QAction(qicon, text, this);
	if(name)
		act->setObjectName(name);
	if(shortcut.isEmpty()==false) {
		act->setShortcut(shortcut);
		act->setProperty("defaultshortcut", shortcut);
	}
	if(tip.isEmpty()==false)
		act->setStatusTip(tip);

	if(name!=0 && name[0]!='\0')
		customacts_.append(act);
	return act;
}

void MainWindow::initActions()
{
	// File actions
	new_ = makeAction("newdocument", "document-new.png", tr("&New"), tr("Start a new drawing"), QKeySequence::New);
	open_ = makeAction("opendocument", "document-open.png", tr("&Open..."), tr("Open an existing drawing"), QKeySequence::Open);
	save_ = makeAction("savedocument", "document-save.png",tr("&Save"),tr("Save drawing to file"),QKeySequence::Save);
	saveas_ = makeAction("savedocumentas", "document-save-as.png", tr("Save &As..."), tr("Save drawing to a file with a new name"));
	quit_ = makeAction("exitprogram", "system-log-out.png", tr("&Quit"), tr("Quit the program"), QKeySequence("Ctrl+Q"));
	quit_->setMenuRole(QAction::QuitRole);

	// The saving actions are initially disabled, as we have no image
	save_->setEnabled(false);
	saveas_->setEnabled(false);

	connect(new_,SIGNAL(triggered()), this, SLOT(showNew()));
	connect(open_,SIGNAL(triggered()), this, SLOT(open()));
	connect(save_,SIGNAL(triggered()), this, SLOT(save()));
	connect(saveas_,SIGNAL(triggered()), this, SLOT(saveas()));
	connect(quit_,SIGNAL(triggered()), this, SLOT(close()));

	// Session actions
	host_ = makeAction("hostsession", 0, tr("&Host..."),tr("Share your drawingboard with others"));
	join_ = makeAction("joinsession", 0, tr("&Join..."),tr("Join another user's drawing session"));
	logout_ = makeAction("leavesession", 0, tr("&Leave"),tr("Leave this drawing session"));
	_lockSession = makeAction("locksession", 0, tr("Lo&ck the board"), tr("Prevent changes to the drawing board"));
	_lockSession->setCheckable(true);
	_closeSession = makeAction("denyjoins", 0, tr("&Deny joins"), tr("Prevent new users from joining the session"));
	_closeSession->setCheckable(true);
	_changetitle = makeAction("changetitle", 0, tr("Change &title..."), tr("Change the session title"));
	logout_->setEnabled(false);

	adminTools_ = new QActionGroup(this);
	adminTools_->setExclusive(false);
	adminTools_->addAction(_lockSession);
	adminTools_->addAction(_closeSession);
	adminTools_->addAction(_changetitle);
	adminTools_->setEnabled(false);

	connect(host_, SIGNAL(triggered()), this, SLOT(host()));
	connect(join_, SIGNAL(triggered()), this, SLOT(join()));
	connect(logout_, SIGNAL(triggered()), this, SLOT(leave()));
	connect(_changetitle, SIGNAL(triggered()), this, SLOT(changeSessionTitle()));

	// Drawing tool actions
	pentool_ = makeAction("toolpen", "draw-freehand.png", tr("&Pen"), tr("Draw with hard strokes"), QKeySequence("P"));
	pentool_->setCheckable(true);

	brushtool_ = makeAction("toolbrush", "draw-brush.png", tr("&Brush"), tr("Draw with smooth strokes"), QKeySequence("B"));
	brushtool_->setCheckable(true); brushtool_->setChecked(true);

	erasertool_ = makeAction("tooleraser", "draw-eraser.png", tr("&Eraser"), tr("Draw with the background color"), QKeySequence("E"));
	erasertool_->setCheckable(true);

	pickertool_ = makeAction("toolpicker", "color-picker.png", tr("&Color picker"), tr("Pick colors from the image"), QKeySequence("I"));
	pickertool_->setCheckable(true);

	linetool_ = makeAction("toolline", "todo-line.png", tr("&Line"), tr("Draw straight lines"), QKeySequence("U"));
	linetool_->setCheckable(true);

	recttool_ = makeAction("toolrect", "draw-rectangle.png", tr("&Rectangle"), tr("Draw unfilled rectangles"), QKeySequence("R"));
	recttool_->setCheckable(true);

	annotationtool_ = makeAction("tooltext", "draw-text.png", tr("&Annotation"), tr("Add annotations to the picture"), QKeySequence("A"));
	annotationtool_->setCheckable(true);

	selectiontool_ = makeAction("toolselectrect", "select-rectangular", tr("&Select"), tr("Select areas for copying"));
	selectiontool_->setCheckable(true);

	// A default
	lasttool_ = brushtool_;

	_drawingtools = new QActionGroup(this);
	_drawingtools->setExclusive(true);
	_drawingtools->addAction(pentool_);
	_drawingtools->addAction(brushtool_);
	_drawingtools->addAction(erasertool_);
	_drawingtools->addAction(pickertool_);
	_drawingtools->addAction(linetool_);
	_drawingtools->addAction(recttool_);
	_drawingtools->addAction(annotationtool_);
	_drawingtools->addAction(selectiontool_);
	connect(_drawingtools, SIGNAL(triggered(QAction*)), this, SLOT(selectTool(QAction*)));

	// Edit actions
	_copy = makeAction("copyvisible", "edit-copy", tr("&Copy visible"), tr("Copy selected area to the clipboard"), QKeySequence::Copy);
	_copylayer = makeAction("copylayer", "edit-copy", tr("Copy layer"), tr("Copy selected area of the current layer to the clipboard"));
	_paste = makeAction("paste", "edit-paste", tr("&Paste"), tr("Paste an image onto the canvas"), QKeySequence::Paste);

	_copy->setEnabled(false);
	_copylayer->setEnabled(false);

	connect(_copy, SIGNAL(triggered()), this, SLOT(copyVisible()));
	connect(_copylayer, SIGNAL(triggered()), this, SLOT(copyLayer()));
	connect(_paste, SIGNAL(triggered()), this, SLOT(paste()));

	// View actions
	zoomin_ = makeAction("zoomin", "zoom-in.png",tr("Zoom &in"), QString(), QKeySequence::ZoomIn);
	zoomout_ = makeAction("zoomout", "zoom-out.png",tr("Zoom &out"), QString(), QKeySequence::ZoomOut);
	zoomorig_ = makeAction("zoomone", "zoom-original.png",tr("&Normal size"), QString(), QKeySequence(Qt::CTRL + Qt::Key_0));
	rotateorig_ = makeAction("rotatezero", "view-refresh.png",tr("&Reset rotation"), tr("Drag the view while holding ctrl-space to rotate"), QKeySequence(Qt::CTRL + Qt::Key_R));

	fullscreen_ = makeAction("fullscreen", 0, tr("&Full screen"), QString(), QKeySequence("F11"));
	fullscreen_->setCheckable(true);

	hideannotations_ = makeAction("toggleannotations", 0, tr("Hide &annotations"), QString());
	hideannotations_->setCheckable(true);

	connect(zoomin_, SIGNAL(triggered()), this, SLOT(zoomin()));
	connect(zoomout_, SIGNAL(triggered()), this, SLOT(zoomout()));
	connect(zoomorig_, SIGNAL(triggered()), this, SLOT(zoomone()));
	connect(rotateorig_, SIGNAL(triggered()), this, SLOT(rotatezero()));
	connect(fullscreen_, SIGNAL(triggered(bool)), this, SLOT(fullscreen(bool)));
	connect(hideannotations_, SIGNAL(triggered(bool)), this, SLOT(toggleAnnotations(bool)));

	// Tool cursor settings
	toggleoutline_ = makeAction("brushoutline", 0, tr("Show brush &outline"), tr("Display the brush outline around the cursor"));
	toggleoutline_->setCheckable(true);

	swapcolors_ = makeAction("swapcolors", 0, tr("Swap colors"), tr("Swap foreground and background colors"), QKeySequence(Qt::Key_X));

	// Settings window action
	settings_ = makeAction(0, 0, tr("&Settings"));
	connect(settings_, SIGNAL(triggered()), this, SLOT(showSettings()));

	// Toolbar toggling actions
	toolbartoggles_ = new QAction(tr("&Toolbars"), this);
	docktoggles_ = new QAction(tr("&Docks"), this);

	// Help actions
	homepage_ = makeAction("dphomepage", 0, tr("&DrawPile homepage"), tr("Open DrawPile homepage with the default web browser"));
	connect(homepage_,SIGNAL(triggered()), this, SLOT(homepage()));
	about_ = makeAction("dpabout", 0, tr("&About DrawPile"), tr("Show information about DrawPile"));
	about_->setMenuRole(QAction::AboutRole);
	connect(about_,SIGNAL(triggered()), this, SLOT(about()));
}

void MainWindow::createMenus()
{
	QMenu *filemenu = menuBar()->addMenu(tr("&File"));
	filemenu->addAction(new_);
	filemenu->addAction(open_);
	recent_ = filemenu->addMenu(tr("Open recent"));
	filemenu->addAction(save_);
	filemenu->addAction(saveas_);
	filemenu->addSeparator();
	filemenu->addAction(quit_);

	connect(recent_, SIGNAL(triggered(QAction*)),
			this, SLOT(openRecent(QAction*)));

	QMenu *editmenu = menuBar()->addMenu(tr("&Edit"));
	editmenu->addAction(_copy);
	editmenu->addAction(_copylayer);
	editmenu->addAction(_paste);

	QMenu *viewmenu = menuBar()->addMenu(tr("&View"));
	viewmenu->addAction(toolbartoggles_);
	viewmenu->addAction(docktoggles_);
	viewmenu->addSeparator();
	viewmenu->addAction(zoomin_);
	viewmenu->addAction(zoomout_);
	viewmenu->addAction(zoomorig_);
	viewmenu->addAction(rotateorig_);
	viewmenu->addAction(fullscreen_);
	viewmenu->addAction(hideannotations_);

	QMenu *sessionmenu = menuBar()->addMenu(tr("&Session"));
	sessionmenu->addAction(host_);
	sessionmenu->addAction(join_);
	sessionmenu->addAction(logout_);
	sessionmenu->addSeparator();
	sessionmenu->addAction(_lockSession);
	sessionmenu->addAction(_closeSession);
	sessionmenu->addAction(_changetitle);

	QMenu *toolsmenu = menuBar()->addMenu(tr("&Tools"));
	toolsmenu->addActions(_drawingtools->actions());
	toolsmenu->addSeparator();
	toolsmenu->addAction(toggleoutline_);
	toolsmenu->addAction(swapcolors_);
	toolsmenu->addSeparator();
	toolsmenu->addAction(settings_);

	//QMenu *settingsmenu = menuBar()->addMenu(tr("Settings"));

	QMenu *helpmenu = menuBar()->addMenu(tr("&Help"));
	helpmenu->addAction(homepage_);
	helpmenu->addSeparator();
	helpmenu->addAction(about_);
}

void MainWindow::createToolbars()
{
	QMenu *togglemenu = new QMenu(this);
	// File toolbar
	QToolBar *filetools = new QToolBar(tr("File tools"));
	filetools->setObjectName("filetoolsbar");
	togglemenu->addAction(filetools->toggleViewAction());
	filetools->addAction(new_);
	filetools->addAction(open_);
	filetools->addAction(save_);
	filetools->addAction(saveas_);
	addToolBar(Qt::TopToolBarArea, filetools);

	// Drawing toolbar
	QToolBar *drawtools = new QToolBar("Drawing tools");
	drawtools->setObjectName("drawtoolsbar");
	togglemenu->addAction(drawtools->toggleViewAction());

	drawtools->addActions(_drawingtools->actions());
	drawtools->addSeparator();
	drawtools->addAction(zoomin_);
	drawtools->addAction(zoomout_);
	drawtools->addAction(zoomorig_);
	drawtools->addAction(rotateorig_);
	drawtools->addSeparator();

	// Create color button
	fgbgcolor_ = new widgets::DualColorButton(drawtools);

	connect(swapcolors_, SIGNAL(triggered()),
			fgbgcolor_, SLOT(swapColors()));

	connect(fgbgcolor_,SIGNAL(foregroundClicked()),
			this, SLOT(setForegroundColor()));

	connect(fgbgcolor_,SIGNAL(backgroundClicked()),
			this, SLOT(setBackgroundColor()));

	// Create color changer dialogs
	fgdialog_ = new dialogs::ColorDialog(tr("Foreground color"), true, false, this);
	connect(fgdialog_, SIGNAL(colorSelected(QColor)),
			fgbgcolor_, SLOT(setForeground(QColor)));

	bgdialog_ = new dialogs::ColorDialog(tr("Background color"), true, false, this);
	connect(bgdialog_, SIGNAL(colorSelected(QColor)),
			fgbgcolor_, SLOT(setBackground(QColor)));

	drawtools->addWidget(fgbgcolor_);

	addToolBar(Qt::TopToolBarArea, drawtools);

	toolbartoggles_->setMenu(togglemenu);
}

void MainWindow::createDocks()
{
	QMenu *toggles = new QMenu(this);
	createToolSettings(toggles);
	createColorBoxes(toggles);
	createPalette(toggles);
	createUserList(toggles);
	createLayerList(toggles);
	createNavigator(toggles);
	tabifyDockWidget(hsv_, rgb_);
	tabifyDockWidget(hsv_, palette_);
	tabifyDockWidget(_userlist, _layerlist);
	docktoggles_->setMenu(toggles);
}

void MainWindow::createNavigator(QMenu *toggles)
{
	navigator_ = new widgets::Navigator(this, _canvas);
	navigator_->setAllowedAreas(Qt::LeftDockWidgetArea|Qt::RightDockWidgetArea);
	toggles->addAction(navigator_->toggleViewAction());
	addDockWidget(Qt::RightDockWidgetArea, navigator_);
}

void MainWindow::createToolSettings(QMenu *toggles)
{
	_toolsettings = new widgets::ToolSettingsDock(this);
	_toolsettings->setObjectName("toolsettingsdock");
	_toolsettings->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	connect(this, SIGNAL(toolChanged(tools::Type)), _toolsettings, SLOT(setTool(tools::Type)));
	toggles->addAction(_toolsettings->toggleViewAction());
	addDockWidget(Qt::RightDockWidgetArea, _toolsettings);
	connect(fgbgcolor_, SIGNAL(foregroundChanged(const QColor&)), _toolsettings, SLOT(setForeground(const QColor&)));
	connect(fgbgcolor_, SIGNAL(backgroundChanged(const QColor&)), _toolsettings, SLOT(setBackground(const QColor&)));
}

void MainWindow::createUserList(QMenu *toggles)
{
	_userlist = new widgets::UserList(this);
	_userlist->setObjectName("userlistdock");
	_userlist->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	toggles->addAction(_userlist->toggleViewAction());
	addDockWidget(Qt::RightDockWidgetArea, _userlist);
}

void MainWindow::createLayerList(QMenu *toggles)
{
	_layerlist = new widgets::LayerListDock(this);
	_layerlist->setObjectName("layerlistdock");
	_layerlist->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	toggles->addAction(_layerlist->toggleViewAction());
	addDockWidget(Qt::RightDockWidgetArea, _layerlist);
}

void MainWindow::createPalette(QMenu *toggles)
{
	palette_ = new widgets::PaletteBox(tr("Palette"), this);
	palette_->setObjectName("palettedock");
	toggles->addAction(palette_->toggleViewAction());

	connect(palette_, SIGNAL(colorSelected(QColor)),
			fgbgcolor_, SLOT(setForeground(QColor)));

	addDockWidget(Qt::RightDockWidgetArea, palette_);
}

void MainWindow::createColorBoxes(QMenu *toggles)
{
	rgb_ = new widgets::ColorBox("RGB", widgets::ColorBox::RGB, this);
	rgb_->setObjectName("rgbdock");
	toggles->addAction(rgb_->toggleViewAction());

	hsv_ = new widgets::ColorBox("HSV", widgets::ColorBox::HSV, this);
	hsv_->setObjectName("hsvdock");
	toggles->addAction(hsv_->toggleViewAction());

	connect(fgbgcolor_,SIGNAL(foregroundChanged(QColor)),
			rgb_, SLOT(setColor(QColor)));
	connect(fgbgcolor_,SIGNAL(foregroundChanged(QColor)),
			hsv_, SLOT(setColor(QColor)));

	connect(rgb_, SIGNAL(colorChanged(QColor)),
			fgbgcolor_, SLOT(setForeground(QColor)));
	connect(hsv_, SIGNAL(colorChanged(QColor)),
			fgbgcolor_, SLOT(setForeground(QColor)));

	addDockWidget(Qt::RightDockWidgetArea, rgb_);
	addDockWidget(Qt::RightDockWidgetArea, hsv_);
}



