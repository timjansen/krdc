/*
     krdpview.h, implementation of the KRdpView class
     Copyright 2002 Arend van Beelen jr. <arend@auton.nl>

     This program is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published by
     the Free Software Foundation; either version 2 of the License, or (at
     your option) any later version.

     This program is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
     or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
     for more details.

     You should have received a copy of the GNU General Public License along
     with this program; if not, write to the Free Software Foundation, Inc.,
     51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

     For any questions, comments or whatever, you may mail me at: arend@auton.nl
*/
#include <fixx11h.h>
#include <kdialog.h>
#include <klocale.h>
#include <kmessagebox.h>
#include <k3process.h>
#include <kwallet.h>
#include <kpassworddialog.h>

#include <kvbox.h>
#include <QTimer>
#include <QX11EmbedContainer>
#include <QCoreApplication>
#include <QEvent>

#undef Bool

#include "krdpview.h"
#include "rdphostpref.h"
#include "rdpprefs.h"

bool rdpAppDataConfigured = false;
extern KWallet::Wallet *wallet;

static KRdpView *krdpview;

// constructor
KRdpView::KRdpView(QWidget *parent,
                   const QString &host, int port,
                   const QString &user, const QString &password,
                   int flags, const QString &domain,
                   const QString &shell, const QString &directory,
		   const QString &caption) :
  KRemoteView(parent),
  m_host(host),
  m_port(port),
  m_user(user),
  m_password(password),
  m_flags(flags),
  m_domain(domain),
  m_shell(shell),
  m_directory(directory),
  m_quitFlag(false),
  m_process(NULL),
  m_caption(caption),
  m_viewOnly(true)
{
	krdpview = this;
	setFixedSize(16, 16);
	if(m_port <= 0)
	{
		m_port = TCP_PORT_RDP;
	}

	m_container = new QX11EmbedContainer(this);
	m_container->installEventFilter(this);
}

// destructor
KRdpView::~KRdpView()
{
	startQuitting();
	delete m_container;
}

// filter out key and mouse events to the container if we are view only
//FIXME: X11 events are passed to the app before getting caught in the Qt event processing
bool KRdpView::eventFilter(QObject *obj, QEvent *event)
{
    if (m_viewOnly) {
        if (event->type() == QEvent::KeyPress ||
            event->type() == QEvent::KeyRelease ||
            event->type() == QEvent::MouseButtonDblClick ||
            event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::MouseButtonRelease ||
            event->type() == QEvent::MouseMove )
            return true;
    }
    return KRemoteView::eventFilter(obj, event);
}

// returns the size of the framebuffer
QSize KRdpView::framebufferSize()
{
	return m_container->minimumSizeHint();
}

// returns the suggested size
QSize KRdpView::sizeHint() const
{
	return maximumSize();
}

// start closing the connection
void KRdpView::startQuitting()
{
  kDebug() << "About to quit" << endl;
	m_quitFlag = true;
	if(m_process != NULL)
	{
	  m_container->discardClient();
	  // FIXME: we proably need to kill the rdesktop process here.
	}
}

// are we currently closing the connection?
bool KRdpView::isQuitting()
{
	return m_quitFlag;
}

// return the host we're connected to
QString KRdpView::host()
{
	return m_host;
}

// return the port number we're connected on
int KRdpView::port()
{
	return m_port;
}

// open a connection
bool KRdpView::start()
{
	SmartPtr<RdpHostPref> hp, rdpDefaults;
	bool useKWallet = false;

	if(!rdpAppDataConfigured)
	{
		HostPreferences *hps = HostPreferences::instance();

		hp = SmartPtr<RdpHostPref>(hps->createHostPref(m_host,
		                                              RdpHostPref::RdpType));
		int wv = hp->width();
		int hv = hp->height();
		int cd = hp->colorDepth();
		QString kl = hp->layout();
		bool kwallet = hp->useKWallet();
		if(hp->askOnConnect())
		{
			// show preferences dialog
			KDialog *dlg = new KDialog( this );
			dlg->setObjectName( "rdpPrefDlg" );
			dlg->setModal( true );
			dlg->setCaption( i18n( "RDP Host Preferences for %1", m_host ) );
			dlg->setButtons( KDialog::Ok | KDialog::Cancel );
			dlg->setDefaultButton( KDialog::Ok );
			dlg->showButtonSeparator( true );

			KVBox *vbox = new KVBox( this );
			dlg->setMainWidget( vbox );

			RdpPrefs *prefs = new RdpPrefs( vbox );
			QWidget *spacer = new QWidget( vbox );
			vbox->setStretchFactor( spacer, 10 );

			prefs->setRdpWidth( wv );
			prefs->setRdpHeight( hv );
			prefs->setResolution();
			prefs->setColorDepth(cd);
			prefs->setKbLayout( keymap2int( kl ) );
			prefs->setShowPrefs( true );
			prefs->setUseKWallet(kwallet);

			if ( dlg->exec() == QDialog::Rejected )
				return false;

			wv = prefs->rdpWidth();
			hv = prefs->rdpHeight();
			kl = int2keymap( prefs->kbLayout() );
			hp->setAskOnConnect( prefs->showPrefs() );
			hp->setWidth(wv);
			hp->setHeight(hv);
			hp->setColorDepth( prefs->colorDepth() );
			hp->setLayout(kl);
			hp->setUseKWallet(prefs->useKWallet());
			hps->sync();
		}

		useKWallet = hp->useKWallet();
	}

	m_container->show();
	m_container->setWindowTitle( m_caption );

	m_process = new K3Process(m_container);
	*m_process << "rdesktop";
	*m_process << "-g" << (QString::number(hp->width()) + 'x' + QString::number(hp->height()));
	*m_process << "-k" << hp->layout();
	if(!m_user.isEmpty())     { *m_process << "-u" << m_user; }

	if(m_password.isEmpty() && useKWallet ) {
		QString krdc_folder = "KRDC-RDP";

		// Bugfix: Check if wallet has been closed by an outside source
		if ( wallet && !wallet->isOpen() ) {
			delete wallet; wallet=0;
		}

		// Do we need to open the wallet?
		if ( !wallet ) {
			QString walletName = KWallet::Wallet::NetworkWallet();
			wallet = KWallet::Wallet::openWallet(walletName);
		}

		if (wallet && wallet->isOpen()) {
			bool walletOK = wallet->hasFolder(krdc_folder);
			if (walletOK == false) {
				walletOK = wallet->createFolder(krdc_folder);
			}

			if (walletOK == true) {
				wallet->setFolder(krdc_folder);
				if ( wallet->hasEntry(m_host) ) {
					wallet->readPassword(m_host, m_password);
				}
			}

			if ( m_password.isEmpty() ) {
				//There must not be an existing entry. Let's make one.
				KPasswordDialog dlg(this);
                dlg.setPrompt( i18n("Please enter the password.") );
				if (dlg.exec() == KPasswordDialog::Accepted) {
					m_password = dlg.password();
					wallet->writePassword(m_host, m_password);
				}
			}
		}
	}

	if(!m_password.isEmpty()) {
		*m_process << "-p" << m_password;
	}

	*m_process << "-X" << QString::number(m_container->winId());
	*m_process << "-a" << QString::number(hp->colorDepth());
	*m_process << (m_host + ':' + QString::number(m_port));

	kDebug() << "K3Process args: " << m_process->args() << endl;

	setStatus(Connecting);
	connect(m_process, SIGNAL(processExited(K3Process *)), SLOT(processDied(K3Process *)));
	connect(m_process, SIGNAL(receivedStderr(K3Process *, char *, int)), SLOT(receivedStderr(K3Process *, char *, int)));
	connect(m_container, SIGNAL(clientClosed()), SLOT(connectionClosed()));
	connect(m_container, SIGNAL(clientIsEmbedded()), SLOT(connectionOpened()));
	if(!m_process->start(K3Process::NotifyOnExit, K3Process::Stderr))
	{
		KMessageBox::error(0, i18n("Could not start rdesktop; make sure rdesktop is properly installed."),
		                      i18n("rdesktop Failure"));
		return false;
	}

	return true;
}

void KRdpView::switchFullscreen(bool on)
{
	if(on == true)
	{
		m_container->grabKeyboard();
	}
}

// captures pressed keys
void KRdpView::pressKey(XEvent *e)
{
	m_container->grabKeyboard();
}

bool KRdpView::viewOnly()
{
	return m_viewOnly;
}

void KRdpView::setViewOnly(bool s)
{
	m_viewOnly = s;
}

void KRdpView::connectionOpened()
{
  kDebug() << "Connection opened" << endl;
	QSize size = m_container->minimumSizeHint();
	kDebug () << "Size hint: " << size.width() << " " << size.height() << endl;
	setStatus(Connected);
	setFixedSize(size);
	resize(size);
	// m_container->adjustSize() ?
	m_container->setFixedSize(size);
	emit changeSize(size.width(), size.height());
	emit connected();
	setFocus();
}

void KRdpView::connectionClosed()
{
	emit disconnected();
	setStatus(Disconnected);
	m_quitFlag = true;
}

void KRdpView::processDied(K3Process */*proc*/)
{
	if(m_status == Connecting)
	{
		setStatus(Disconnected);
		if(m_clientVersion.isEmpty())
		{
			KMessageBox::error(0, i18n("Connection attempt to host failed."),
			                      i18n("Connection Failure"));
		}
		else
		{
			// FIXME: rdesktop 1.3.2 (or maybe 1.4.0) should be released by the time KDE 3.3 is released
			KMessageBox::error(0, i18n("The version of rdesktop you are using (%1) is too old:\n"
			                           "rdesktop 1.3.2 or greater is required.", m_clientVersion),
			                      i18n("rdesktop Failure"));
		}
		emit disconnectedError();
	}
}

void KRdpView::receivedStderr(K3Process */*proc*/, char *buffer, int /*buflen*/)
{
	QString output(buffer);
	QString line;
	int i = 0;
	while(!(line = output.section('\n', i, i)).isEmpty())
	{
		if(line.startsWith("Version "))
		{
			m_clientVersion = line.section(' ', 1, 1);
			m_clientVersion = m_clientVersion.left(m_clientVersion.length() - 1);
			return;
		} else {
		  kDebug() << "Process error output: " << line << endl;
		}
		i++;
	}
}

#include "krdpview.moc"
