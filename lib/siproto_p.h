#ifndef SIPROTO_P_H
#define SIPROTO_P_H

#include <QObject>

class SiProto;

class CommandReceiver : public QObject {
	Q_OBJECT
	public:
		CommandReceiver( SiProto *si, unsigned char cmnd, QObject *parent = 0 );

		bool waitForCommand( int timeoutms );

		QByteArray data;
		unsigned char command;
		int cn;
		bool extendedCommand;
		bool haveit;
		bool havenak;

	public slots:
		void gotCommand( unsigned char cmnd, const QByteArray &d, int cn );
		void gotNAK();

};

#endif // SIPROTO_P_H
