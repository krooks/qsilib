#ifndef DIALOG_H
#define DIALOG_H

#include <QDialog>
#include "siproto.h"

class QStatusBar;
class QStandardItemModel;
class QAbstractItemModel;
class QAbstractButton;

namespace Ui {
    class Dialog;
}

class Dialog : public QDialog
{
    Q_OBJECT

public:
    explicit Dialog(QWidget *parent = 0);
    ~Dialog();

private:
	enum tasks {
		taskNone,
		taskGetTime,
		taskSetTime,
		taskReadConf,
		taskWriteConf,
		taskEraseBackup,
		taskGetBackup
	};
	Ui::Dialog *ui;
	SiProto si;
	bool inslavemode;
	QStatusBar *bar;
	QStandardItemModel *sicardmodel, *backupmodel;

	void saveModelAsCSV( QAbstractItemModel * );
	void fillCardBlocksCombo( unsigned int val = 0xFF );
	void doGetTime( int count );
	void gotReadConf( unsigned char addr, const QByteArray &ba );
	void writeConfEnd();

	void enableButtons( bool enable );

	void startTask( tasks t, int commands );

	tasks currenttask;
	int gettimecount, settimecount;
	int timediffcount;
	int timediffsum;
	int timemindiff, timemaxdiff;
	int settimefix;
	QByteArray latestconf;

	QAbstractButton *cancelButton;

private slots:
	void on_enableFeedback_clicked( bool v );
	void on_operatingMode_currentIndexChanged(int index);
	void on_resetBackup_clicked();
 void on_operatingMode_activated(int index);
 void on_saveStationBackup_clicked();
 void on_clearBackup_clicked();
 void buttonClicked( QAbstractButton *b );
	void on_readBackup_clicked();
 void on_saveSICards_clicked();
 void on_clearSICards_clicked();
 void on_writeStationConf_clicked();
 void on_readStationConf_clicked();
 void on_setTimeButton_clicked();
 void on_getTimeButton_clicked();
 void siStatusMsg( const QString & );
 void siCardRead( const SiCard & );
 void gotBackupSiCard( const SiCard *);
 void gotBackupPunch( const PunchBackupData &);
 void readBackupBlock( int num, int total );
 void updateProgressBar();

 void stopTask();
 void gotMSMode( SiProto::MSMode );
 void gotTime( const QDateTime &, const QDateTime & );
 void gotSetTime( const QDateTime & );
 void gotSystemValu( unsigned char addr, const QByteArray &ba );
 void gotSetSystemValu( unsigned char addr, const QByteArray &ba );

 void badParameter( const QString &msg );
};

#endif // DIALOG_H
