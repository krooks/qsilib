#ifndef _QSERIAL_H_
#define _QSERIAL_H_

#ifdef USING_PCH
#include "precomp.h"
#else
#include <QIODevice>
#include <QQueue>
#include <QThread>
#endif

#if ( defined( __linux__ ) | defined( __APPLE__ ) )
#include <termios.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

class QSocketNotifier;
class QFile;

class QSerial : public QIODevice
{
	Q_OBJECT

	public:
		QSerial( QObject *parent = 0 );
		~QSerial( void );

		bool open( const QString &dev, int speed );
		void close( void );

		bool isOpen() const;
		
		qint64 bytesAvailable();
#if ( defined( __linux__ ) | defined( __APPLE__ ) )
		bool canReadLine() const;
#endif
		bool isSequential() const;
		QString lasterror;

		void readIntoBuffer( void );

		bool atEnd( void ) const;

		bool waitForReadyRead( int msecs );

	protected:
		qint64 readData(char *data, qint64 maxlen);
		qint64 writeData(const char *data, qint64 len);

	private slots:
		void canReadNotification( int );

	private:
		Q_DISABLE_COPY(QSerial)
		void setupSocketNotifiers( void );
#if ( defined( __linux__ ) | defined( __APPLE__ ) )
		struct termios newtio, oldtio;
		int io_port;
#else
		HANDLE fh;

		class watcher : public QThread {
			public:
				void run( void ) {
					while( 1 ) {
						if ( s->bytesAvailable() > 0 )
							s->canReadNotification(0);
						usleep( 100 );
					}
				}
				QSerial *s;
		};
		watcher wt;
#endif
		
		QQueue<unsigned char> buffer;

		QSocketNotifier *readSocketNotifier;

		QFile *logFile;
		bool isatend;
};
#endif
