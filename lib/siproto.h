#ifndef SIPROTO_H
#define SIPROTO_H

#include <QStringList>
#include <QMap>
#include <QDateTime>
#include <QVariant>

#include "qserial.h"

class PunchBackupData {
	public:
		PunchBackupData( unsigned char *d, int size, double swm, int scn );

		QTime t;
		QDate d;
		int cardnum;
		int dayofweek; // 0-Sun, 1-Mon
		int cn;

		QString dumpstr( void ) const;
	private:
		unsigned char SI2, SI1, SI0, TH, TL, TD, TSS, SI3, DATE1, DATE0, MS;
};

class PunchingRecord {
	public:
		PunchingRecord() :
			cn( 0 )
			{};
		PunchingRecord( int controlnr, const QTime &t=QTime() ) :
			cn( controlnr ),
			time( t )
			{}
		PunchingRecord(const unsigned char *d);
		int cn;
		int ptd;
		bool pm;
		int dayofweek;
		int weekcounter;

		QTime time;
		QDateTime fulltime;

	private:
		enum {
			PTD = 0,
			CN = 1,
			PTH = 2,
			PTL = 3
		};
};

class SiCard {
	public:
		SiCard() : 
			valid( false )
		{};
		
		void setEventStartTime( const QDateTime &dt );

		int getCardNumber() const;
		QDateTime getFullStartTime() const;
		QDateTime getFullFinishTime() const;
		QDateTime getFullCheckTime() const;
		const QList<PunchingRecord> & getPunches() const;

		QByteArray getRawData() const;

		void print() const;
		virtual QString dumpstr( void ) const;

	protected:
		int countrycode;
		int clubcode;
		int cardnum;
		int intcardnum;
		int startnum;
		PunchingRecord starttime;
		PunchingRecord checktime;
		PunchingRecord finishtime;
		QDateTime fullstarttime, fullchecktime, fullfinishtime;
		QList<PunchingRecord> punches;
		int punchingcounter;

		QDateTime inittime;
		bool valid;

		QTime siTime( unsigned char s2, unsigned char s1 );
		int word2int( const unsigned char *d );
		void calcFullTimes( void );
		QDateTime closestVariant( const QDateTime &from, const QTime &t );

		QByteArray rawData;
};

class SiCard89pt : public SiCard
{
	public:
		void reset();
		void addBlock( int bn, const QByteArray &data128 );
		//QString dumpstr( void ) const;
	private:
		enum {
			UID0 = 0x00,
			UID1 = 0x01,
			UID2 = 0x02,
			UID3 = 0x03,
			clearcheckstart = 8,
			startstart = 12,
			finishstart = 16,
			cnh = 20,
			cnl = 21,
			point = 22,
			system2 = 23,
			SI3 = 24,
			SI2 = 25,
			SI1 = 26,
			SI0 = 27,
			vumonth = 28,
			vuyear = 29,
			system1 = 30,
			system0 = 31,
			card9pstart = 56,
			card8pstart = 136
		};
};

class SiCard6 : public SiCard
{
	public:
		SiCard6(const QByteArray &data);
		SiCard6() : SiCard() {};
		void resolveBackupBlocks( const QList<QByteArray> &blocks );
		void reset();
		void addBlock( int bn, const QByteArray &data128 );
		QString dumpstr( void ) const;

	private:
		void addPunchBlock( int firstindex, const QByteArray &data );
		void addInfoBlock1( const unsigned char *d );
		void addInfoBlock2( const unsigned char *d );

		enum {
			CN3 = 0x0A,
			CN2 = 0X0B,
			CN1 = 0X0C,
			CN0 = 0X0D,
			punchingPointer = 0x10, // last punched control station ( 2 bytes ), punch count( 1 byte ), pc+1( 1byte )
			finishstart = 0x14,
			startstart = 0x18,
			checkstart = 0x1C,
			clearstart = 0x20,
			startnumstart = 0x28,
			contclassstart = 0x2c,
			lastnamestart = 0x30,
			firstnamestart = 0x44,
			bla = 0x54,
			countrystart = 0x58,
			clubstart = 0x5C,
			useridstart = 0x00,
			phonestart = 0x10,
			emailstart = 0x20,
			streetstart = 0x44,
			citystart = 0x58,
			zipstart = 0x68,
			sexstart = 0x70,
			dayofbirthstart = 0x74,
			dopstart = 0x7C
		};
		QString firstname, lastname, contclass;
		QString country, club;
		QString userid, phone, email, street, city, zip, dayofbirth;
		unsigned char sex;
		int startnum;
};

class SiCard5 : public SiCard
{
	public:
		SiCard5( const QByteArray &data128 );
		QString dumpstr( void ) const;
		
	private:
		enum Fields {
			CI6 = 0x01,
			CI5 = 0x02,
			CI4 = 0x03,
			CN1 = 0x04,
			CN0 = 0x05,
			CNS = 0x06,
			SN1 = 0x11,
			SN0 = 0x12,
			ST2 = 0x13,
			ST1 = 0x14,
			FT2 = 0x15,
			FT1 = 0x16,
			PC = 0x17,
			CT2 = 0x19,
			CT1 = 0x1A,
			SW = 0x1B,
			SNS = 0x1C,
			CS = 0x1D,
		};

		QByteArray rawdata;
		int softwareversion;
		int checksum;
};

class SiProto : public QObject {

	Q_OBJECT
	friend class CommandReceiver;

	public:
		SiProto( QObject *parent = 0 );

		enum MSMode {
			DirectCommunication = 0x4D,
			SlaveCommunication = 0x53
		};
		enum SystemValueAddresses {
			FullData = 0x00,		   // Data size: 80
			SWVersion = 0x05,		// Data size: 3
			BackupMemoryAddres = 0x1C,	// Data size: 7
			CardBlocks = 0x33,		// Data size: 1
			StationMode = 0x71,		// Data size: 1
			StationCode = 0x72,		// Data size: 1
			ProtocolConf = 0x74	    // Data size: 1
		};
		enum ConfigFlags {
			FlagExtendedProtocol = 0x01,
			FlagAutoSendOut = 0x02,
			FlagHandshake = 0x04,
			FlagPasswdOnly = 0x08,
			FlagReadAfterPunc = 0x10
		};
		enum StationMode {
			StationControl = 2,
			StationStart = 3,
			StationFinish = 4,
			StationReadSICards = 5,
			StationClear = 7,
			StationCheck = 10
		};

		QList<SiCard*> GetCardBackupData();
		bool StartGetBackup();
		bool StartGetPunchBackupData();
		bool StartGetCardBackupData();
		QList<PunchBackupData> GetPunchBackupData();
		bool searchAndOpen( void );
		void setFeedbackEnabled( bool enabled );
		bool tryDevice( const QString &d );
		bool trySettingsDevice( void );

		bool SetMSMode( MSMode mode, bool block = true, int*cn=NULL );
		bool SetSystemValue( unsigned char addr, const QByteArray &ba );
		bool SetSystemValue( unsigned char addr, const QByteArray &ba, QByteArray *rdata, int *cn=NULL );
		bool GetSystemValue( unsigned char addr, unsigned char len );
		bool GetSystemValue( unsigned char addr, unsigned char len, QByteArray *ba, int *cn=NULL );
		bool SetBaudRate( int speed, int *cn=NULL );
		bool GetTime();
		bool GetTime( QDateTime *dt, int *cn = NULL );
		bool SetTime( const QDateTime &sdt );
		bool SetTime( const QDateTime &sdt, QDateTime *dt, int *cn = NULL );
		bool ResetBackup();
		bool ResetBackup( int *cn );

		void stopTasks();

		void setDoHandshake( bool v ) {
			doHandshake = v;
		}
		static SiCard cardFromData( const QByteArray ba );

	private:
		QStringList fullDeviceList( void );
		QStringList searchdevicelist;

		QByteArray &addDLE( QByteArray &data );
		QByteArray &removeDLE( QByteArray &data );
		QByteArray timeForSI( const QDateTime &dt );

		QDateTime handleGetTime( const QByteArray &data, bool extendedCommand );

		bool sendCommand( unsigned char command, const QByteArray &data  = QByteArray() );
		bool sendACK( void );
		bool sendNAK( void );

		bool getCommand( unsigned char &cmnd, QByteArray &data, int *cn = NULL );
		bool readCommand( unsigned char &cmnd, QByteArray &data );
		bool GetDataFromBackup( unsigned int startaddr, unsigned int readsize );
		bool GetDataFromBackup( unsigned int startaddr, unsigned int readsize, unsigned int *readaddr, QByteArray *ba, int *cn = NULL );
		void updateSystemInfo(unsigned char addr, const QByteArray &data);
		void handlePunchBackupData( unsigned int addr, const QByteArray &data, int cn );
		void handleCardBackupData( unsigned int addr, const QByteArray &data );

		struct systeminfo {
			double swversion;
			int backupmemaddr;
			int cardblocks;
			int stationmode;
			int protocolconf;

			int backuprecordsize;
			int backupreadsize;
		} lastreadinfo;
		int backupreadpointer;
	enum ProtocolCharacer {
		STX = 0x02, // Start of text, first byte to be transmitted
		ETX = 0x03, // End of text, last byte to be transmitted
		ACK = 0x06, // Positive handshake return
		NAK = 0x15, // Negative handshake return
		DLE = 0x10  // DeLimitEr to be inserted before data characters 00-1F
	};
	enum Commands {
		BaseCommandGetSICard5 = 0x31,
		BaseCommandSICard5Detected = 0x46,
		BaseCommandSICard6Detected = 0x66,
		BaseCommandGetSICard6 = 0x61,
		BaseCommandSetMSMode = 0x70,
		BaseCommandGetBackupData = 0x74,
		BaseCommandEraseBackupData = 0x75,
		BaseCommandSetTime = 0x76, // Needs testing
		BaseCommandGetTime = 0x77,
		CommandGetBackupData = 0x81,
		CommandSetSystemValue = 0x82, // Needs testing
		CommandGetSystemValue = 0x83,
		CommandGetSICard5 = 0xB1,
		CommandGetSICard6 = 0xE1,
		CommandSICard5Detected = 0xE5,
		CommandSICard6Detected = 0xE6,
		CommandSICardRemoved = 0xE7,
		CommandSICard89ptDetected = 0xE8,
		CommandGetSICard89pt = 0xEF,
		CommandSetMSMode = 0xF0,
		CommandEraseBackupData = 0xF5,
		CommandSetTime = 0xF6,
		CommandGetTime = 0xF7,
		CommandTurnOffStation = 0xF6, // Needs testing : par(HEX) 60
		CommandSetBaudRate = 0xFE
	};
	enum BaseSICard5InserRemove {
		SICard5Inserted = 0x49,
		SiCard5Removed = 0x4F
	};
	static QMap<int, int> baseCommands;
	static int timeoutforcommands;

	QSerial serial;

	bool extendedmode;
	bool doHandshake;
	bool autoAccept;
	bool siCard6Inserted;
	bool startingbackup;
	bool readingpunchbackup;
	bool readingcardbackup;
	QByteArray sibuf;

	enum formatting_settings {
		none,
		SPORTident,
		SPORTidentNW,
		SimpleXOR,
		always
	};

	formatting_settings DLEformatting;
	formatting_settings LENformatting;
	formatting_settings CRCformatting;

	bool safeInitialisation;
	bool STXtwice;
	SiCard6 card6forread;
	SiCard89pt card89ptforread;
	int card6blocksread;
	int lastcard6block;

	private slots:
		void serialReadyRead();

	signals:
		void sentCommand( unsigned char cmnd, const QByteArray &data );
		void gotCommand( unsigned char cmnd, const QByteArray &data, int cn );
		void gotNAK();
		void cardInserted( const QString &ver, const QVariant num );
		void statusMessage( const QString &msg );
		void cardRead( const SiCard & );
		void backupCard( const SiCard * );
		void backupPunch( const PunchBackupData & );
		void backupBlockNumFrom( int num, int from );

		void gotTime( const QDateTime &dt, const QDateTime &ct, int cn );
		void gotSetTime( const QDateTime &dt, int cn );
		void gotMSMode( SiProto::MSMode m, int cn );
		void gotSystemValue( unsigned char addr, const QByteArray &ba, int cn );
		void gotSetSystemValue( unsigned char addr, const QByteArray &ba, int cn );
		void gotBackupData( unsigned int addr, const QByteArray &ba, int cn );
		void gotErasedBackup();
};

#endif
