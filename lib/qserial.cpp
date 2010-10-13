
#include "qserial.h"

#ifndef USING_PCH
#include <QFile>
#include <QSocketNotifier>
#endif

#if ( defined( __linux__ ) | defined( __APPLE__ ) )
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#else
#include <windows.h>
#endif
#ifdef __APPLE__
#include <sys/ioctl.h>
#endif

#define DEBUG_SERIAL	1

QSerial::QSerial( QObject *parent ) :
	QIODevice( parent )
#if ( defined( __linux__ ) | defined( __APPLE__ ) )
	,io_port( -1 )
#else
	,fh( INVALID_HANDLE_VALUE )
#endif
	,readSocketNotifier(NULL)
{
}

QSerial::~QSerial( void )
{
#ifdef __linux__
	if ( io_port != -1 ) {
		tcsetattr( io_port, TCSANOW, &oldtio );
	}
#endif
}

bool QSerial::isSequential() const
{
	return true;
}

bool QSerial::isOpen() const
{
#ifdef __linux__
	return io_port != -1;
#else
	return QIODevice::isOpen();
#endif
}

bool QSerial::open( const QString &dev, int speed )
{
	isatend = false;
#if ( defined( __linux__ ) | defined( __APPLE__ ) )
#ifdef DEBUG_SERIAL
	logFile = new QFile( "/tmp/qserial.log", this );
	logFile->open( QIODevice::Append );
	QByteArray ba = "Open file:";
	ba += dev;
	ba += "\n";
	logFile->write( ba );
#endif
	io_port = ::open( dev.toUtf8(), O_RDWR|O_NOCTTY|O_NONBLOCK );
	if ( io_port == -1 ) {
		perror( "Failed to open serial" );
		return false;
	}
#if ( defined( __APPLE__ ) )
	if (ioctl(io_port, TIOCEXCL)==-1)
		qWarning( "Failed to set exclusiv open" );
	// Clear O_NONBLOCK flag.
	if (fcntl( io_port, F_SETFL,0)==-1)
		qWarning( "Failed to clear NONBLOCK flag" );
#endif
	tcgetattr( io_port, &oldtio );

	bzero( &newtio, sizeof( newtio ) );
	int s;
	switch( speed ) {
		case 4800:
			s = B4800; break;
		case 9600:
			s = B9600; break;
		case 19200:
			s = B19200; break;
		case 38400:
			s = B38400; break;
		case 57600:
			s = B57600; break;
		case 115200:
			s = B115200; break;
		default:
			s = B9600; break;
	}
	cfsetispeed( &newtio, s );
	cfsetospeed( &newtio, s );
	newtio.c_cflag |= CS8 | CLOCAL | CREAD;
	newtio.c_iflag = IGNBRK | IGNPAR;

	newtio.c_cc[VTIME] = 10;
        newtio.c_cc[VMIN] = 1;

	tcflush( io_port, TCIFLUSH );

	tcsetattr( io_port, TCSANOW, &newtio );

	QIODevice::open( ReadWrite );
	setupSocketNotifiers();
	return true;
#else
	fh = CreateFileA( dev.toAscii(), GENERIC_READ | GENERIC_WRITE, 
			FILE_SHARE_READ|FILE_SHARE_WRITE, NULL,
			OPEN_EXISTING, 0, NULL );
			//  OPEN_EXISTING, FILE_ATTRIUBTE_NORMAL, NULL );
	if ( fh == INVALID_HANDLE_VALUE ) {
		LPVOID lpMsgBuf;
		LPVOID lpDisplayBuf;
		DWORD dw = GetLastError(); 

		FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | 
				FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				dw,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPTSTR) &lpMsgBuf,
				0, NULL );
		
		lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, 
				(lstrlen((LPCTSTR)lpMsgBuf)+lstrlen((LPCTSTR)"open")+40)*sizeof(TCHAR)); 
		lasterror = "";
		for( int i=0;i<(lstrlen((LPCTSTR)lpMsgBuf));i++ )
			lasterror += (((LPCTSTR)lpMsgBuf)[i]);
		//lasterror.sprintf( "%i: open failed with err %d: %s", (lstrlen((LPCTSTR)lpMsgBuf)), (int)dw, (const char *)(LPCTSTR)lpMsgBuf );
		return false;
	}
	int s;
	switch( speed ) {
		case 4800:
			s = CBR_4800; break;
		case 9600:
			s = CBR_9600; break;
		case 19200:
			s = CBR_19200; break;
		case 38400:
			s = CBR_38400; break;
		case 57600:
			s = CBR_57600; break;
		case 115200:
			s = CBR_115200; break;
		default:
			s = CBR_9600; break;
	}
	COMMCONFIG comcfg;
	if ( GetCommState(fh, &comcfg.dcb) ) {
		comcfg.dcb.BaudRate = s;
		comcfg.dcb.ByteSize = 8;
		comcfg.dcb.Parity = NOPARITY;
		comcfg.dcb.StopBits = ONESTOPBIT;
		comcfg.dcb.fAbortOnError = TRUE;
		comcfg.dcb.fOutxCtsFlow = FALSE;
		comcfg.dcb.fOutxDsrFlow = FALSE;
		comcfg.dcb.fDtrControl = DTR_CONTROL_DISABLE;
		comcfg.dcb.fRtsControl = RTS_CONTROL_DISABLE;
		comcfg.dcb.fDsrSensitivity = FALSE;
		comcfg.dcb.fTXContinueOnXoff = TRUE;
		comcfg.dcb.fOutX = FALSE;
		comcfg.dcb.fInX = FALSE;
		comcfg.dcb.fBinary = TRUE;
		comcfg.dcb.fParity = TRUE;
		SetCommState( fh, &comcfg.dcb);
		GetCommState( fh, &comcfg.dcb );
	}
	QIODevice::open( ReadWrite );
	setupSocketNotifiers();
	return true;
#endif
}

void QSerial::close( void )
{
#ifdef WIN32
	CloseHandle( fh );
	fh = INVALID_HANDLE_VALUE;
#else
	if ( readSocketNotifier ) {
		readSocketNotifier->deleteLater();
		readSocketNotifier = NULL;
	}
	::close( io_port );
	io_port = 1;
#endif
	QIODevice::close();
}

void QSerial::setupSocketNotifiers( void )
{
#ifdef WIN32
	wt.s = this;
	wt.start();
#else
	readSocketNotifier = new QSocketNotifier( io_port, QSocketNotifier::Read, this );
	connect( readSocketNotifier, SIGNAL( activated(int) ), this,
			SLOT( canReadNotification( int ) ) );
#endif
}

void QSerial::canReadNotification( int )
{
	readIntoBuffer();
	emit readyRead();
}

#define MAXQUEUESIZE	1024

void QSerial::readIntoBuffer( void )
{
#if ( defined( __linux__ ) | defined( __APPLE__ ) )
		char buf[BUFSIZ];
	ssize_t len = 0;
	if ( waitForReadyRead(0))
		len = ::read( io_port, buf, BUFSIZ );
#ifdef DEBUG_SERIAL
	QByteArray ba = "<-("+QByteArray::number( (int)len )+") ";
	for( int j=0;j<len;j++ )
		ba += "0x"+QByteArray::number( (unsigned char )buf[j], 16 )+" ";
	ba += "\n";
	logFile->write( ba );
#endif
	if ( len < 1 ) {
		isatend = true;
		return;
	}
	for( int i=0;i<len;i++ )
		buffer.enqueue( buf[i] );
	while( buffer.count() > MAXQUEUESIZE )
		buffer.dequeue();
#endif
}

qint64 QSerial::readData(char *data, qint64 maxlen)
{
	if ( !buffer.isEmpty() ) {
		int i;
		for( i=0;!buffer.isEmpty() && i<maxlen;i++ )
			data[i] = buffer.dequeue();
		return i;
	}
#ifdef WIN32
	int retVal=0;
	COMSTAT Win_ComStat;
	DWORD Win_BytesRead=0;
	DWORD Win_ErrorMask=0;
	ClearCommError(fh, &Win_ErrorMask, &Win_ComStat);
	if (Win_ComStat.cbInQue &&
		(!ReadFile(fh, (void*)data, (DWORD)maxlen, &Win_BytesRead, NULL)
		 || Win_BytesRead==0)) {
		printf( "Error while reading\n" );
		return -1;
	} else
		retVal = ((int)Win_BytesRead);
	return retVal;
#else
	char buf[BUFSIZ];
	ssize_t len = 0;
	if ( waitForReadyRead(0) )
		len = ::read( io_port, buf, BUFSIZ );
#ifdef DEBUG_SERIAL
	QByteArray ba = "<-("+QByteArray::number( (uint)len )+") ";
	for( int j=0;j<len;j++ )
		ba += "0x"+QByteArray::number( (unsigned char )buf[j], 16 )+" ";
	ba += "\n";
	logFile->write( ba );
	logFile->flush();
#endif
	if ( len < 1 ) {
		//qDebug( "len: %i", len );
		//isatend = true;
		return len;
	}
	if ( maxlen ) 
		memcpy( data, buf, ( len >= maxlen ? maxlen : len ) );
	for( int i=maxlen;i<len;i++ )
		buffer.enqueue( buf[i] );
	while( buffer.count() > MAXQUEUESIZE )
		buffer.dequeue();
	return ( len >= maxlen ? maxlen : len );
#endif
}

qint64 QSerial::writeData(const char *data, qint64 len)
{
#ifdef WIN32
	int retVal=0;
	DWORD Win_BytesWritten;
	if (!WriteFile(fh, (void*)data, (DWORD)len, &Win_BytesWritten, NULL)) {
		printf( "Failed to write to serial\n" );
		retVal=-1;
	} else {
		retVal=((int)Win_BytesWritten);
	}

	return retVal;
#else
	qint64 wlen = ::write( io_port, data, len );
#ifdef DEBUG_SERIAL
	QByteArray ba = "<-("+QByteArray::number( len )+") ";
	for( int j=0;j<len;j++ )
		ba += "0x"+QByteArray::number( (unsigned char )data[j], 16 )+" ";
	ba += "\n";
	logFile->write( ba );
	logFile->flush();
#endif
	return wlen;
#endif
}

#ifdef WIN32
qint64 QSerial::bytesAvailable() {
	if (isOpen()) {
		DWORD Errors;
		COMSTAT Status;
		bool success=ClearCommError(fh, &Errors, &Status);
		if (success) {
			return Status.cbInQue + QIODevice::bytesAvailable();
		}
		return (unsigned int)-1;
	}
	return 0;
}
#else
qint64 QSerial::bytesAvailable() {
	return buffer.count()+QIODevice::bytesAvailable();
}

bool QSerial::canReadLine() const
{
	return buffer.contains( '\n' ) || buffer.contains( 13 ) || QIODevice::canReadLine();
}
#endif

bool QSerial::atEnd( void ) const
{
	return isatend;
}

bool QSerial::waitForReadyRead( int msecs )
{
#if ( defined( __linux__ ) | defined( __APPLE__ ) )
	if ( io_port == -1 )
		return false;
	fd_set rset;
	FD_ZERO( &rset );

	FD_SET( io_port, &rset );

	struct timeval tv;
	tv.tv_sec = (int)(msecs/1000);
	tv.tv_usec = msecs-(tv.tv_sec*1000);
	int ret = select( io_port+1, &rset, NULL, NULL,  &tv );
	if ( ret > 0 )
		return true;
	return false;
#endif
}
