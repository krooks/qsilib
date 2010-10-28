
#include "siproto_p.h"
#include "siproto.h"

#include <QDir>
#include <QTimer>
#include <QSettings>
#include <QApplication>

//#define SI_COMM_DEBUG	1

#include "crc529.c"
#include <unistd.h>

QMap<int, int> SiProto::baseCommands;
int SiProto::timeoutforcommands = 2000; // In milliseconds

CommandReceiver::CommandReceiver(SiProto *si, unsigned char cmnd, QObject *parent) :
		QObject( parent ),
		command( cmnd ),
		extendedCommand( false ),
		haveit( false ),
		havenak( false )
{
	connect( si, SIGNAL(gotCommand(unsigned char,QByteArray,int)), SLOT(gotCommand(unsigned char,QByteArray,int)) );
	connect( si, SIGNAL(gotNAK()), SLOT(gotNAK()) );
}

void CommandReceiver::gotCommand(unsigned char cmnd, const QByteArray &d, int cnum)
{
	if ( cmnd != command && cmnd != SiProto::baseCommands[command] )
		return;
	data = d;
	cn = cnum;
	extendedCommand = ( cmnd == command );
	haveit = true;
}

void CommandReceiver::gotNAK()
{
	havenak = true;
}

bool CommandReceiver::waitForCommand( int timeoutms)
{
	QTime t;
	t.start();
	while( !havenak && !haveit && (timeoutms && t.elapsed() < timeoutms) ) {
		qApp->processEvents();
	}
	return haveit;
}

int siCardNum( unsigned char SI0, unsigned char SI1, unsigned char SI2, unsigned char SI3 = 0x00 )
{
	int cardnum = (SI1<<8);
	cardnum |= (SI0);
	if ( SI2 > 1  && SI2 < 5 )
		cardnum += (SI2*100000 );
	if ( SI2 > 4 )
		cardnum += SI2<<16;
	cardnum |= (SI3<<24);
	return cardnum;
}

PunchBackupData::PunchBackupData( unsigned char *a, int size, double sw, int scn )
{
	SI3 = 0x0;
	MS = 0x0;
	bool hasdate = false, hasms = false, hastss = false;
	if ( size == 6 ) {
		SI1=a[0]; SI0=a[1]; TH=a[2]; TL=a[3]; TD=a[4]; SI2=a[5];
	} else {
		if ( sw < 5.55 ) {
			SI1=a[0]; SI0=a[1]; TH=a[2]; TL=a[3]; TD=a[4]; SI2=a[5]; TSS=a[6]; SI3=a[7];
			hastss = true;
		} else {
			SI2=a[0]; SI1=a[1]; SI0=a[2]; DATE1=a[3]; DATE0=a[4]; TH=a[5]; TL=a[6]; MS=a[7];
			hasdate = true;
			hasms = true;
		}

	}
	/*
	qDebug( "DATE1: 0x%2X, DATE0: 0x%2X", DATE1, DATE0 );
	qDebug( "1: %i, 2: %i, 3: %i, 4: %i, 5: %i",
			(DATE1&0xFC)>>2, (DATE1&0x03),
			(DATE0&0xC0)>>6, (DATE0&0x3E)>>1,
			(DATE0&0x1) );
	qDebug( "Mon: %i", (DATE1&0x03)<<2|(DATE0&0xC0)>>6 );
	*/
	cardnum = siCardNum(SI0, SI1, SI2, SI3);
	d = QDate();
	t = QTime(0, 0).addSecs( (TH<<8)|TL );
	t = t.addMSecs( MS*1000/256 );
	if ( !hasdate && (TD & 0x1) )
		t = t.addSecs( 43200 );
	if ( hasdate ) {
		d = QDate( ((DATE1&0xFC)>>2)+1000, (DATE1&0x03)<<2|(DATE0&0xC0)>>6, (DATE0&0x3E)>>1 );
		if ( DATE0&0x1 )
			t = t.addSecs( 43200 );
	}
	dayofweek = (TD&0x0E) >> 1;
	cn = scn;
}

QString PunchBackupData::dumpstr( void ) const
{
	QString s;
	s.sprintf( "CardNum: %i, Time: %s, dayofweek: %i\n", cardnum, qPrintable( t.toString() ), dayofweek );
	return s;
}

QDateTime SiCard::closestVariant( const QDateTime &from, const QTime &t )
{
	QDateTime dt = from;
	dt.setTime( t );
	int bestsec = qAbs(dt.secsTo( from ));
	QDateTime besttime = dt;
	dt = dt.addSecs( -43200 );
	if ( qAbs(dt.secsTo(from)) < bestsec ) {
		bestsec = qAbs(dt.secsTo(from));
		besttime = dt;
	}
	dt = dt.addSecs( -43200 );
	if ( qAbs(dt.secsTo(from)) < bestsec ) {
		bestsec = qAbs(dt.secsTo(from));
		besttime = dt;
	}
	dt = dt.addSecs( 129600 );
	if ( qAbs(dt.secsTo(from)) < bestsec ) {
		bestsec = qAbs(dt.secsTo(from));
		besttime = dt;
	}
	dt = dt.addSecs( 43200 );
	if ( qAbs(dt.secsTo(from)) < bestsec ) {
		bestsec = qAbs(dt.secsTo(from));
		besttime = dt;
	}
	return besttime;
}

void SiCard::print( void ) const
{
	qDebug("%s", qPrintable( dumpstr() ) );
}

QDateTime SiCard::getFullStartTime() const
{
	return fullstarttime;
}

QDateTime SiCard::getFullFinishTime() const
{
	return fullfinishtime;
}

QDateTime SiCard::getFullCheckTime() const
{
	return fullchecktime;
}

const QList<PunchingRecord> &SiCard::getPunches() const
{
	return punches;
}

void SiCard::calcFullTimes( void )
{
	fullchecktime = fullstarttime = fullfinishtime = QDateTime();
	QDateTime prevtime = inittime;
	if ( checktime.time.isValid() )
		prevtime = fullchecktime = closestVariant( prevtime, starttime.time );
	if ( starttime.time.isValid() )
		prevtime = fullstarttime = closestVariant( prevtime, starttime.time );
	for( int i=0;i<punchingcounter && i<punches.count();i++ ) {
		prevtime = punches[i].fulltime = closestVariant( prevtime, punches[i].time);
	}
	if ( finishtime.time.isValid() )
		fullfinishtime = closestVariant(prevtime, finishtime.time);
}

void SiCard::setEventStartTime( const QDateTime &dt )
{
	inittime = dt;
	calcFullTimes();
}

int SiCard::getCardNumber() const
{
	return cardnum;
}

QByteArray SiCard::getRawData() const
{
	return rawData;
}

QString SiCard::dumpstr( void ) const
{
	if ( !valid ) {
		return "Invalid card";
	}
	QString s;
	s += QString( "Card number: %0\n").arg(cardnum);
	if ( !starttime.time.isNull() )
		s += QString("Start: %0\n").arg( starttime.time.toString() );
	QString tmp;
	tmp.sprintf( "Country code: 0x%02X, %i\n", countrycode, countrycode );
	s += tmp;
	tmp.sprintf( "Club code: 0x%02X, %i\n", clubcode, clubcode );
	s += tmp;
	s += QString("Start number: %0\n").arg(startnum);
	s += QString("Punching counter: %0\n").arg(punchingcounter);
	for( int i=0;i<punches.count();i++ )
		s += QString( "P %0: CN: %1, %2\n").arg(i).arg(punches.at(i).cn).arg( punches.at(i).time.toString() );
	return s;
}

int SiCard::word2int(const unsigned char *d)
{
	return ((d[0]<<24)|(d[1]<<16)|(d[2]<<8)|d[3]);
}

QTime SiCard::siTime( unsigned char s2, unsigned char s1 )
{
	if ( s2 == 0xEE && s1 == 0xEE )
		return QTime();
	return QTime(0, 0).addSecs( (s2<<8)|s1 );
}

void SiCard89pt::reset()
{
	punchingcounter = 0;
	punches.clear();
	rawData.clear();
}

void SiCard89pt::addBlock(int bn, const QByteArray &data)
{
	unsigned char *d = (unsigned char *)data.data();
	if ( bn == 0 ) {
		intcardnum = siCardNum(d[SI0], d[SI1], d[SI2], d[SI3]);
		cardnum = intcardnum&0xFFFFFF;
		punchingcounter = d[point];
	}
	int pstart;
	int si3 = intcardnum>>24;
	if ( si3 == 0x1 )
		pstart = card9pstart/4;
	else if ( si3 == 0x2 )
		pstart = card8pstart/4;
	if ( bn == 0 ) {
		for( int i=0;i+pstart<32 && i<punchingcounter;i++ )
			punches.append(PunchingRecord(d+((i+pstart)*4)));
	} else if ( bn == 1 ) {
		for( int i=0;i<32 && ((128-pstart)/4)+i<punchingcounter;i++ )
			punches.append(PunchingRecord(d+(i*4)));
	}
}

SiCard6::SiCard6(const QByteArray &data)
{
	QList<QByteArray> blocks;
	for( int i=0;i<data.length();i+=128 ) {
		blocks.append( data.mid(i,128 ) );
	}
	qDebug( "Card from data: %i, blocks: %i", data.length(), blocks.count() );
	resolveBackupBlocks( blocks );
}

void SiCard6::reset()
{
	rawData.clear();
	punchingcounter = 0;
	punches.clear();
}

void SiCard6::resolveBackupBlocks( const QList<QByteArray> &blocks )
{
	if ( blocks.count() == 2 ) {
		addBlock(6, blocks[0]);
		addBlock(7, blocks[1]);
	} else if ( blocks.count() == 3 ) {
		addBlock(0, blocks[0]);
		resolveBackupBlocks( blocks.mid(1) );
	} else if ( blocks.count() == 6 ) {
		for( int i=0;i<6;i++ )
			addBlock(i+1, blocks[i]);
	} else if ( blocks.count() == 7 ) {
		addBlock(0, blocks[0]);
		resolveBackupBlocks( blocks.mid(1) );
	} else {
		qWarning( "Read backup of card6 with peculiar number of blocks. Don't know what to do" );
	}
}

QString SiCard6::dumpstr() const
{
	QString s;
	s = QString( "SI card 6\n" );
	s+=QString("First/Last name: %0/%1\n").arg(firstname).arg(lastname);
	s+=QString("Country: %0\n").arg(country);
	s+=QString("Club: %0\n").arg(club);
	s+=QString("Start number: %0\n").arg(startnum);
	s+=QString("Class: %0\n").arg(contclass);
	s+=QString("User-id: %0\n").arg(userid);
	s+=QString("Phone number: %0\n").arg(phone);
	s+=QString("E-mail: %0\n").arg(email);
	s+=QString("Street: %0\n").arg(street);
	s+=QString("City: %0\n").arg(city);
	s+=QString("Zip-code: %0\n").arg(zip);
	s+=QString("Day Of Birth: %0\n").arg(dayofbirth);
	s+=QString("Sex: %0\n").arg(sex);
	// user-id, mobile, e-mail, street, city, zip, sex, day of birth, date of product
	s += SiCard::dumpstr();
	return s;
}

void SiCard6::addInfoBlock1(const unsigned char *d)
{
	cardnum = siCardNum(d[CN0], d[CN1], d[CN2], d[CN3]);
	punchingcounter = d[punchingPointer+2];
	QByteArray strdata(36, 0x00);
	memcpy(strdata.data(), d+firstnamestart, 20 );
	firstname = QString::fromUtf8(strdata, 20).trimmed();
	memcpy(strdata.data(), d+lastnamestart, 20 );
	lastname = QString::fromUtf8(strdata, 20).trimmed();
	memcpy(strdata.data(), d+clubstart, 36 );
	club = QString::fromUtf8(strdata, 36).trimmed();
	memcpy(strdata.data(), d+countrystart, 4 );
	country = QString::fromUtf8(strdata, 4).trimmed();
	memcpy(strdata.data(), d+contclassstart, 4 );
	contclass = QString::fromUtf8(strdata, 4).trimmed();
	startnum = word2int(d+startnumstart);

	valid = true;
}

void SiCard6::addInfoBlock2(const unsigned char *d)
{
	// user-id, mobile, e-mail, street, city, zip, sex, day of birth, date of product
	QByteArray strdata(36, 0x00);
	memcpy(strdata.data(), d+useridstart, 16 );
	userid = QString::fromUtf8(strdata, 16).trimmed();
	memcpy(strdata.data(), d+phonestart, 16 );
	phone = QString::fromUtf8(strdata, 16).trimmed();
	memcpy(strdata.data(), d+emailstart, 36 );
	email = QString::fromUtf8(strdata, 36).trimmed();
	memcpy(strdata.data(), d+streetstart, 20 );
	street = QString::fromUtf8(strdata, 20).trimmed();
	memcpy(strdata.data(), d+citystart, 16 );
	city = QString::fromUtf8(strdata, 16).trimmed();
	memcpy(strdata.data(), d+zipstart, 8 );
	zip = QString::fromUtf8(strdata, 8).trimmed();
	memcpy(strdata.data(), d+dayofbirthstart, 8 );
	dayofbirth = QString::fromUtf8(strdata, 8).trimmed();
	sex = d[0x73];
}

void SiCard6::addBlock(int bn, const QByteArray &data)
{
	rawData.append( data );
	if ( data.length() != 128 ) {
		qWarning( "Block with incorrect length: %i", data.length() );
		return;
	}

	if (bn == 0 )
		addInfoBlock1((const unsigned char *)data.data());
	else if ( bn == 1 )
		addInfoBlock2((const unsigned char *)data.data());
	else if ( bn > 5 )
		addPunchBlock((bn-6)*32, data);
	else if ( bn > 1 && bn < 6 )
		addPunchBlock(bn*32, data);
	calcFullTimes();
}

// Initialize from 4 first bytes as in card 6
PunchingRecord::PunchingRecord(const unsigned char *d)
{
	cn = d[CN];
	if ( d[PTH] == 0xEE && d[PTL] == 0xEE )
		time = QTime();
	else
		time = QTime(0, 0).addSecs( (d[PTH]<<8)|d[PTL] );
	dayofweek = (d[PTD]&0x0E)>>1;
	weekcounter = (d[PTD]&0x30)>>4;
	// (d[PTD]&0xC0)>>6; // Value of Control Station High number
	if ( d[PTD] & 0x1 )
		time = time.addSecs( 43200 );
}

void SiCard6::addPunchBlock(int firstindex, const QByteArray &data)
{
	// Make the size of punces list enough to hold this block.
	for( int i=punches.length();i<(firstindex+32) && i<punchingcounter;i++ )
		punches.append(PunchingRecord());
	unsigned char *d = (unsigned char *)data.data();
	for( int i=firstindex;i<(firstindex+32) && i<punchingcounter;i++ ) {
		punches[i] = PunchingRecord(d+((i-firstindex)*4));
	}
}

SiCard5::SiCard5( const QByteArray &data )
{
	if (data.length() != 128 )
		return;
	rawData = data;
#ifdef SI_COMM_DEBUG
	qDebug( "Settings SiCard5 data:" );
	for( int i=0;i<128;i++ )
		qDebug( "%i:%02x - %i, 0x%02X", i, i, (unsigned char)data.at(i), (unsigned char)data.at(i) );
#endif
	countrycode = ((unsigned char)data.at(CI6));
	clubcode = ((unsigned char)data.at(CI5)<<8);
	clubcode |= ((unsigned char)data.at(CI4));
	cardnum = ((unsigned char)data.at(CN1)<<8);
	cardnum |= ((unsigned char)data.at(CN0));
	if ( data.at(CNS) > 1 )
		cardnum += ((unsigned char)data.at(CNS))*100000;
	startnum = ((unsigned char)data.at(SN1)<<8);
	startnum |= ((unsigned char)data.at(SN0));
	if ( data.at(SNS) > 1 )
		startnum += ((unsigned char)data.at(SNS))*100000;
	starttime.time = siTime( data.at(ST2), data.at(ST1) );
	finishtime.time = siTime( data.at(FT2), data.at(FT1) );
	checktime.time = siTime( data.at(CT2), data.at(CT1) );
	softwareversion = (unsigned char)data.at(SW);
	checksum = (unsigned char)data.at(CS);
	if( checksum != 
		(unsigned char)((unsigned char)data.at(SNS)+
						(unsigned char)data.at(SN1)+
						(unsigned char)data.at(SN0)) )
		qWarning( "Failed checksum of si card" );
	punchingcounter = (unsigned char)data.at(PC);
	punches.clear();
	int i;
	for( i=0;i<(punchingcounter-1) && i<31;i++ ) {
		int pstart = 0x21+((int)((i)/5))+(i*3);
		punches.append( PunchingRecord( data.at(pstart), siTime( data.at(pstart+1), data.at(pstart+2) ) ) );
	}
	for(;i<(punchingcounter-1)&&i<37;i++ )
		punches.append( PunchingRecord( data.at(0x20+(i-31)*16) ) );
	valid = true;
	rawdata = data;
	inittime = QDateTime::currentDateTime();
	calcFullTimes();
}


QString SiCard5::dumpstr( void ) const
{
	QString s;
	s = "SI Card 5\n";
	s+= QString( "Software versoin: %0\n").arg(softwareversion);
	QString tmp;
	tmp.sprintf("checksum: %i,0x%02X\n", checksum, checksum );
	s += tmp;
	/*
	unsigned char sn1 = rawdata.at(SN1);
	unsigned char sn0 = rawdata.at(SN0);
	unsigned char sns = rawdata.at(SNS);
	unsigned char st = sn1+sn0+sns;
	qDebug( "Test: %i,0x%02X", st, st );
	*/
	s += SiCard::dumpstr();
	return s;
}

SiProto::SiProto( QObject *parent ) :
		QObject( parent ),
		doHandshake( true ),
		autoAccept( false ),
		siCard6Inserted( false ),
		startingbackup( false ),
		readingpunchbackup( false ),
		readingcardbackup( false )
{
	safeInitialisation = true;
	DLEformatting = SPORTident;
	LENformatting = SPORTident;
	CRCformatting = SPORTident;
	STXtwice = false;

	if ( baseCommands.isEmpty() ) {
		baseCommands.insert( CommandGetSICard6, BaseCommandGetSICard6 );
		baseCommands.insert( CommandGetSICard5, BaseCommandGetSICard5 );
		baseCommands.insert( CommandSICard5Detected, BaseCommandSICard5Detected );
		baseCommands.insert( CommandSICard6Detected, BaseCommandSICard6Detected );
		baseCommands.insert( CommandSetMSMode, BaseCommandSetMSMode );
		baseCommands.insert( CommandGetBackupData, BaseCommandGetBackupData );
		baseCommands.insert( CommandEraseBackupData, BaseCommandEraseBackupData );
		baseCommands.insert( CommandSetTime, BaseCommandSetTime );
		baseCommands.insert( CommandGetTime, BaseCommandGetTime );
		baseCommands.insert( CommandSetBaudRate, 0x7E );
	}
	
	connect( &serial, SIGNAL( readyRead() ), this,
			 SLOT( serialReadyRead() ) );
}

void SiProto::serialReadyRead()
{
	QByteArray tmp = serial.read(100);
	while( tmp.length() > 0 ) {
		sibuf.append( tmp );
		tmp = serial.read(100);
	}
#ifdef SI_COMM_DEBUG
	qDebug( "From serial: %i", sibuf.count() );
	for( int i=0;i<sibuf.size();i++ )
		qDebug( "%i - %x", i, (unsigned char)sibuf.at(i) );
#endif
	if ( sibuf.count() ) {
		unsigned char cmnd;
		QByteArray data;
		QVariant cnum = QVariant();
		QString cardver = QString::null;
		int cn;
		if( getCommand( cmnd, data, &cn ) ) {
			switch ( cmnd ) {
			case 0x46: // FI ( SICard5 detected ) baseCommands[CommandSICard5Detected];
				if ( data.at(0) != SICard5Inserted )
					break;
			case CommandSICard5Detected:
				cardver = "5";
				if ( doHandshake )
					sendCommand( CommandGetSICard5 );
				break;
			case CommandSICard6Detected: case BaseCommandSICard6Detected:
				cardver = "6";
				if ( doHandshake ) {
					siCard6Inserted = true;
					card6forread.reset();
					GetSystemValue(CardBlocks, 1);
				}
				break;
			case CommandSICard89ptDetected:
				cardver = "8/9/p";
				if ( doHandshake ) {
					card89ptforread.reset();
					QByteArray ba;
					ba.append((char)0);
					sendCommand( CommandGetSICard89pt,ba );
				}
				break;
			case CommandGetSICard89pt:
				{
					unsigned char bn = (unsigned char)data.at(0);
					card89ptforread.addBlock(bn, data.mid(1));
					if ( bn == 0 ) {
						QByteArray ba;
						ba.append((char)1);
						sendCommand( CommandGetSICard89pt,ba );
					} else if ( bn == 1 ) {
						emit cardRead( card89ptforread );
					}
					break;
				}
			case CommandGetSICard5: case BaseCommandGetSICard5:
				{
					SiCard5 card( data );
					card.print();
					emit cardRead( card );
					if ( autoAccept )
						sendACK();
					break;
				}
			case CommandGetSICard6: case BaseCommandGetSICard6:
				{
					unsigned char bn = (unsigned char)data.at(0);
					card6forread.addBlock(bn, data.mid(1));
					if ( bn == lastcard6block ) {
						card6forread.print();
						emit cardRead( card6forread );
						if ( autoAccept )
							sendACK();
					}
				}
				break;
			case CommandGetTime: case BaseCommandGetTime:
				{
					QDateTime ct = QDateTime::currentDateTime();
					emit gotTime( handleGetTime(data, cmnd == CommandGetTime), ct, cn );
					break;
				}
			case CommandSetTime: case BaseCommandSetTime:
				{
					emit gotSetTime( handleGetTime(data, cmnd==CommandSetTime), cn );
					break;
				}
			case CommandSetMSMode: case BaseCommandSetMSMode:
				emit gotMSMode((MSMode)data.at(0), cn);
				break;
			case CommandGetSystemValue:
				updateSystemInfo( data.at(0), data.mid(1) );
				if ( siCard6Inserted && data.length() > 1 && ((unsigned char )data.at(0) == CardBlocks) ) {
					siCard6Inserted = false;
					unsigned char b = (unsigned char)data.at(1);
					lastcard6block = 7;
					if ( b != 0xFF ) {
						lastcard6block = 0;
						b = b>>1;
						while( b ) {
							lastcard6block++;
							b = b>>1;
						}
					}
					QByteArray ba;
					ba.append((unsigned char)0x08);
					sendCommand( CommandGetSICard6, ba );
					break;
				} else if ( startingbackup ) {
					if ( (backupreadpointer-0x100) % lastreadinfo.backuprecordsize ) {
						emit badParameter(QString("Bad starting address from backup memory read. Should be multiple of %1 since 0x100").arg(lastreadinfo.backuprecordsize) );
					} else if ( backupreadendaddr > 0x20000) {
						emit badParameter("Backup read size too big" );
					} else if ( backupreadendaddr && (backupreadendaddr-0x100) % lastreadinfo.backuprecordsize ) {
						emit badParameter(QString("Bad read size for backup memory read. Should be multiple of %1").arg(lastreadinfo.backuprecordsize ) );

					} else if ( lastreadinfo.stationmode == StationReadSICards ) {
						readingcardbackup = true;
						card6blocksread = 0;
					} else {
						readingpunchbackup = true;
					}
					if ( backupreadendaddr == 0 ) {
						backupreadendaddr = lastreadinfo.backupmemaddr;
					}
					startingbackup = false;
				}
				if ( readingpunchbackup || readingcardbackup) {
					int stilltoread = backupreadendaddr-backupreadpointer;
					if ( stilltoread > 0 )
						GetDataFromBackup( backupreadpointer, (stilltoread>lastreadinfo.backupreadsize ? lastreadinfo.backupreadsize : stilltoread));
					else {
						emit backupBlockNumFrom(0, 0);
					}
					break;
				}
				emit gotSystemValue( data.at(0), data.mid(1), cn );
				break;
			case CommandSetSystemValue:
				emit gotSetSystemValue( data.at(0), data.mid(1), cn );
				break;
			case CommandEraseBackupData: case BaseCommandEraseBackupData:
				emit gotErasedBackup();
				break;
			case CommandGetBackupData: case BaseCommandGetBackupData:
				{
					unsigned int readaddr = ((unsigned char)data.at(0))<<16;
					readaddr |= ((unsigned char)data.at(1))<<8;
					readaddr |= ((unsigned char)data.at(2));
					if ( readingpunchbackup )
						handlePunchBackupData(readaddr, data.mid(3), cn);
					if ( readingcardbackup )
						handleCardBackupData(readaddr, data.mid(3));
					if ( readingpunchbackup || readingcardbackup ) {
						backupreadpointer += data.length()-3;
						int stilltoread = backupreadendaddr-backupreadpointer;
						int total = (backupreadendaddr-0x100)/lastreadinfo.backuprecordsize;
						int blocknum = (backupreadpointer-0x100)/lastreadinfo.backuprecordsize;
						emit backupBlockNumFrom(blocknum, total);
						if ( stilltoread < lastreadinfo.backuprecordsize ) {
							if ( card6blocksread )
								resolveCard6Backup(NULL);
							break;
						}
						GetDataFromBackup( backupreadpointer, (stilltoread>lastreadinfo.backupreadsize ? lastreadinfo.backupreadsize : stilltoread));
						break;
					}
					emit gotBackupData(readaddr, data.mid(3),cn);
					break;
				}
			default:
				break;
			};
			if ( cmnd == CommandSICard5Detected || cmnd ==BaseCommandSICard5Detected || cmnd == CommandSICard6Detected || cmnd == BaseCommandSICard6Detected || cmnd == CommandSICard89ptDetected ) {
				unsigned char *t = (unsigned char *)data.data();
				if ( data.length() == 6 )
					t += 2;
				if ( data.length() >= 4 )
					cnum = siCardNum(t[3],t[2],t[1],t[0]);
			}
			if ( !cardver.isNull() ) {
				emit cardInserted(cardver,cnum);
				QString cnumstring = "";
				if ( cnum.isValid() ) {
					QVariant tmp = cnum.toInt()&0xFFFFFF;
					cnumstring = " : "+tmp.toString();
				}
				emit statusMessage( "Inserted SI-Card "+cardver+cnumstring );
			}
		}
	};
}

QStringList SiProto::fullDeviceList( void )
{
#if defined( __APPLE__ ) || defined( __linux__ )
#if defined( __APPLE__ )
	QDir d( "/dev", "cu.*" );
#elif defined( __linux__ )
	QDir d( "/dev", "ttyUSB*" );
#endif
	d.setFilter( QDir::System );
	QFileInfoList fl = d.entryInfoList();
	QStringList sl;
	for( int i=0;i<fl.count();i++ ) {
		sl.append( fl.at(i).filePath() );
	}
	return sl;
#endif
}

bool SiProto::tryDevice( const QString &d )
{
	if ( serial.isOpen() )
		serial.close();
	if ( !serial.open( d, 38400 ) ) {
		emit statusMessage( "Failed to open serial device: "+d );
		return false;
	}
	extendedmode = true;
	CommandReceiver cr( this, CommandSetMSMode );
	if( SetMSMode( DirectCommunication )  && cr.waitForCommand(1000) ) {
		emit statusMessage( "SportIdent at "+d+" with speed 38400. Using extended mode" );
		return true;
	}
	
	// TODO Problems with close / open 
	serial.close();
	if ( !serial.open( d, 4800 ) ) {
		emit statusMessage( "Failed to open serial device: "+d );
		return false;
	}
	cr.haveit = false;
	if( SetMSMode( DirectCommunication ) && cr.waitForCommand(1000) ) {
		emit statusMessage( "SportIdent at "+d+" with speed 4800. Using extended mode" );
		return true;
	}
	extendedmode = false;
	cr.haveit = false;
	if( SetMSMode( DirectCommunication ) && cr.waitForCommand(1000) ) {
		emit statusMessage( "SportIdent at "+d+" with speed 4800. Not using extended mode" );
		return true;
	}

	emit statusMessage( "Could not find SporIdent on serial device: "+d );
	// TODO Try more modes
	return false;
}

bool SiProto::sendACK( void )
{
	unsigned char ba[] = { (unsigned char)ACK };
	return serial.write( (const char *)ba, 1 );
}

bool SiProto::sendNAK( void )
{
	unsigned char ba[] = { (unsigned char)NAK };
	return serial.write( (const char *)ba, 1 );
}

bool SiProto::sendCommand( unsigned char command, const QByteArray &data )
{
	QByteArray ba;
	if ( safeInitialisation )
		ba.append( 0xFF );
	ba.append( STX );
	if ( STXtwice )
		ba.append( STX );
	if ( !extendedmode ) {
		if ( baseCommands.contains( command ) ) {
			command = baseCommands[command];
		} else
			qWarning( "No base command for 0x%02X, trying to use extended one.", command );
	}
	if ( LENformatting != SPORTidentNW )
		ba.append( command );
	if ( LENformatting == always ||
		 ( LENformatting == SPORTident && command >= 0x80 &&
		   command != 0xC4 ) )
		ba.append( data.length() );
	if ( LENformatting == SPORTidentNW )
		ba.append( data.length()+1 ); // Should actually be +cmnd_length
	if ( LENformatting == SPORTidentNW )
		ba.append( command );
	if ( DLEformatting == always ||
		 (DLEformatting == SPORTident && command < 0x80) ) {
		QByteArray datacopy = data;
		ba.append( addDLE( datacopy ) );
	} else
		ba.append( data );
	if ( CRCformatting == always || CRCformatting == SPORTidentNW || 
		 (CRCformatting == SPORTident &&
		  command >= 0x80 && command != 0xC4 ) ) {
		int pos = 1;
		if ( safeInitialisation )
			pos++;
		if ( STXtwice )
			pos++;
		unsigned int c = crc( ba.length()-pos, (unsigned char *)ba.mid( pos ).data() );
		ba.append( 0xFF & c>>8 );
		ba.append( 0xFF & c );
	}
	// TODO: simpleXOR crc
	ba.append( ETX );
#ifdef SI_COMM_DEBUG
	for( int i=0;i<ba.size();i++ ) {
		qDebug( "Wri: %i - 0x%02X", i, (unsigned char)ba.at(i) );
	}
	QString cm;
	for( int i=0;i<ba.size();i++ ) {
		cm.append( QString::number( (unsigned char )ba.at(i), 16 )+" " );
	}
	qDebug( "Writing: %s", qPrintable( cm ) );
#endif
	if ( serial.write( ba ) == ba.length() ) {
		emit sentCommand(command, data);
		return true;
	}
	return false;
}

QByteArray &SiProto::removeDLE( QByteArray &data )
{
	for( int i=0;i<data.size();i++ ) {
		if ( data.at(i) == DLE )
			data.remove(i, 1 );
	}
	return data;
}

QByteArray &SiProto::addDLE( QByteArray &data )
{
	for( int i=0;i<data.size();i++ ) {
		if ( data.at(i) <= 0x1F ) {
			data.insert( i, DLE );
			i++;
		}
	}
	return data;
}

bool SiProto::getCommand( unsigned char &cmnd, QByteArray &data, int *scn )
{
	if ( !readCommand( cmnd, data ) )
		return false;
	bool hascn = true;
	if ( cmnd == baseCommands[CommandSICard5Detected] )
		hascn = false;
	int cn;
	if ( hascn ) {
		if ( cmnd < 0x80 ) {
			cn = (unsigned char)(data.at(0));
			data = data.mid(1);
		} else {
			cn = (((unsigned char)data.at(0))<<8)|((unsigned char)data.at(1));
			data = data.mid(2);
		}
		if ( scn )
			*scn = cn;
	}
	emit gotCommand( cmnd, data, ( hascn ? cn : -1 ) );
	return true;
}

bool SiProto::readCommand( unsigned char &cmnd, QByteArray &data )
{
	int length;
	bool musttry = false;
	if ( sibuf.length() ) 
		musttry = true;
	while ( musttry || serial.waitForReadyRead( 1000 ) ) {
		if ( !musttry )
			sibuf.append( serial.read(1024) );
		musttry = false;
#ifdef SI_COMM_DEBUG
		qDebug( "readCommand" );
		QString line = "     ";
		for( int i=0;i<16;i++ )
			line += QString( " %1" ).arg((unsigned char)i,2,16,QChar('0'));
		line += "\n";
		line += QString("%1\n").arg( "", 3*16+5, QChar('-') );
		for( int i=0;i<sibuf.length();i++ ) {
			if ( !((i)%16 ) )
				line += QString( "%1 : " ).arg((unsigned char)i,2,16,QChar('0'));
			line += QString( " %1" ).arg((unsigned char)sibuf.at(i),2,16,QChar('0'));
			if ( !((i+1)%16) )
				line += "\n";
			//qDebug( "\t0x%02X", (unsigned char)sibuf.at(i) );
		}
		qDebug( "%s", qPrintable( line ) );
#endif
		if ( sibuf.length() && sibuf.at(0) == NAK ) {
			sibuf = sibuf.mid(1);
			emit statusMessage( "Got NAK response" );
			emit gotNAK();
			return false;
		}
		while( sibuf.length() && sibuf.at(0) != STX ) 
			sibuf = sibuf.mid( 1 );
		if ( sibuf.length() < 2 )
			continue;
		cmnd = sibuf.at(1);
		if ( cmnd >= 0x80 && cmnd != 0xC4 ) {
			if ( sibuf.length() < 3 )
				continue;
			length = (unsigned char)sibuf.at(2);
			if ( sibuf.length() < 6+length )
				continue;
#ifdef SI_COMM_DEBUG
			printf( "Data\n" );
			for( int l=0;l<length;l++ ) {
				printf( " %02X", (unsigned char)sibuf.at(l+3 ) );
				if ( !(l % 10) && l > 5 )
					printf( "\n" );
			}
#endif
			unsigned int creal = (((unsigned char)sibuf.at(length+3))<<8)|((unsigned char)sibuf.at(length+4));
			unsigned int ctest = crc( length+2, (unsigned char *)sibuf.mid( 1 ).data() );
			if ( creal != ctest ) {
				sibuf = sibuf.mid(1);
				qWarning( "CRC Not ok: %x != %x", creal, ctest );
				continue;
			}
			if ( sibuf.at(length+5) != ETX ) {
				sibuf = sibuf.mid(1);
				continue;
			}
			data = sibuf.mid( 3, length );
			sibuf = sibuf.mid( length+6 );
			return true;
		} else {
			int pos = 2;
			while( pos < sibuf.length() ) {
				if ( sibuf.at(pos) == ETX )
					break;
				if ( sibuf.at(pos) == DLE )
					pos += 2;
				else
					pos++;
			}
			if ( sibuf.at(pos) == ETX ) {
		qDebug( "Have ETX" );
				data = sibuf.mid( 2, pos-2 );
				removeDLE( data );
				sibuf = sibuf.mid( pos+1 );
				return true;
			}
		}
	}
	qDebug( "Error: Could not read command..." );
	return false;
}

bool SiProto::GetDataFromBackup( unsigned int startaddr, unsigned int readsize )
{
	if ( readsize == 0 )
		return false;
	QByteArray ba;
	ba.append( (startaddr>>16)&0xFF );
	ba.append( (startaddr>>8)&0xFF );
	ba.append( startaddr&0xFF );
	ba.append( readsize&0xFF );
	return sendCommand( CommandGetBackupData, ba );
}

bool SiProto::GetDataFromBackup( unsigned int startaddr, unsigned int readsize, unsigned int *readaddr, QByteArray *rdata, int *cn )
{
	CommandReceiver cr( this, CommandGetBackupData );
	QByteArray ba;
	ba.append( (startaddr>>16)&0xFF );
	ba.append( (startaddr>>8)&0xFF );
	ba.append( startaddr&0xFF );
	ba.append( readsize&0xFF );
	if ( !sendCommand( CommandGetBackupData, ba ) )
		return false;
	
	if ( !cr.waitForCommand(timeoutforcommands) )
		return false;
	if ( cn )
		*cn = cr.cn;
	if ( readaddr ) {
		*readaddr = ((unsigned char)cr.data.at(0))<<16;
		*readaddr |= ((unsigned char)cr.data.at(1))<<8;
		*readaddr |= ((unsigned char)cr.data.at(2));
	}
	if ( rdata )
		*rdata = cr.data.mid(3);
	return true;
}

bool SiProto::SetSystemValue( unsigned char addr, const QByteArray &data )
{
	QByteArray ba;
	ba.append( addr );
	ba.append( data );
	return sendCommand( CommandSetSystemValue, ba );
}

bool SiProto::SetSystemValue( unsigned char addr, const QByteArray &data,  QByteArray *rdata, int *cn )
{
	CommandReceiver cr( this, CommandSetSystemValue );
	QByteArray ba;
	ba.append( addr );
	ba.append( data );
	if ( !sendCommand( CommandSetSystemValue, ba ) )
		return false;
	if ( !cr.waitForCommand(timeoutforcommands) )
		return false;
	if ( cn )
		*cn = cr.cn;
	if ( rdata )
		*rdata = cr.data.mid(1);
	return true;
}

bool SiProto::GetSystemValue( unsigned char addr, unsigned char len )
{
	QByteArray ba;
	ba.append( addr );
	ba.append( len );
	return sendCommand( CommandGetSystemValue, ba );
}

bool SiProto::GetSystemValue( unsigned char addr, unsigned char len, QByteArray *mem, int *cn )
{
	CommandReceiver cr( this, CommandGetSystemValue );
	QByteArray ba;
	ba.append( addr );
	ba.append( len );
	if ( !sendCommand( CommandGetSystemValue, ba ) )
		return false;
	
	if ( !cr.waitForCommand(timeoutforcommands) )
		return false;
	if ( cn )
		*cn = cr.cn;
	if ( mem )
		*mem = cr.data.mid(1);
	return true;
}

bool SiProto::SetBaudRate( int speed, int *cn )
{
	if ( speed == 4800 )
		speed = 0;
	else if ( speed == 38400 )
		speed = 1;
	if ( speed != 0 && speed != 1 ) {
		qWarning( "Unknown speed" );
		return false;
	}
	sendCommand( CommandSetBaudRate, QByteArray( (const char *)&speed, 1 ) );
	
	unsigned char cmnd;
	QByteArray data;
	if ( !getCommand( cmnd, data, cn ) )
		return false;
	if ( !(cmnd == CommandSetBaudRate || cmnd == baseCommands[CommandSetBaudRate] ) )
		return false;
	if ( data.length() != 1 || (unsigned char)data.at(0) != speed )
		return false;
	return true;
}

QByteArray SiProto::timeForSI(const QDateTime &sdt)
{
	QByteArray sb;

	sb.append( sdt.date().year()-2000 );
	sb.append( sdt.date().month() );
	sb.append( sdt.date().day() );
	unsigned char td = 0x00;
	unsigned int sec = QTime(0,0).secsTo( sdt.time() );
	td |= sdt.date().dayOfWeek()<<1;
	if ( td & 0x0E ) // Sunday is 0
		td &= 0xF2;
	if ( sec >= 43200 ) { // pm
		sec -= 43200;
		td |= 0x01;
	}
	sb.append( td );
	sb.append( (sec>>8)&0xFF );
	sb.append( sec&0xFF );
	if ( extendedmode ) {
		sb.append( sdt.time().msec()*256/1000 );
	}
	return sb;
}

bool SiProto::SetTime( const QDateTime &sdt )
{

	return sendCommand( CommandSetTime, timeForSI(sdt) );
}

bool SiProto::SetTime( const QDateTime &sdt, QDateTime *dt, int *cn )
{
	CommandReceiver cr( this, CommandSetTime );
	sendCommand( CommandSetTime, timeForSI(sdt) );

	if ( !cr.waitForCommand(timeoutforcommands) )
		return false;
	if ( cn )
		*cn = cr.cn;
	if ( dt )
		*dt = handleGetTime(cr.data, cr.extendedCommand);
	return true;
}

bool SiProto::GetTime()
{
	return sendCommand( CommandGetTime );
}

bool SiProto::GetTime( QDateTime *dt, int *cn )
{
	CommandReceiver cr( this, CommandGetTime );
	sendCommand( CommandGetTime );

	if ( !cr.waitForCommand(timeoutforcommands) )
		return false;
	if ( cn )
		*cn = cr.cn;
	if ( dt )
		*dt = handleGetTime(cr.data, cr.extendedCommand);
	return true;
}

QDateTime SiProto::handleGetTime( const QByteArray &data, bool extendedCommand )
{
	QDateTime dt;
	QDate datepart;
	if ( !((unsigned char)data.at(0) == 0xFE &&
		   (unsigned char)data.at(1) == 0xFE &&
		   (unsigned char)data.at(2) == 0xFE ) )
		datepart.setYMD( 2000+data.at(0), data.at(1), data.at(2) );
	unsigned char twd = (unsigned char)data.at(3);
	unsigned int sec = (((unsigned char)data.at(4))<<8)|((unsigned char)data.at(5));
	QTime timepart = QTime(0,0).addSecs( sec );
	if ( twd & 0x01 )
		timepart = timepart.addSecs( 43200 );
	if ( extendedCommand ) {
		unsigned char tss = (unsigned char)data.at(6);
		timepart = timepart.addMSecs( tss*1000/256 );
	}
	dt.setDate( datepart );
	dt.setTime( timepart );
	return dt;
}

bool SiProto::SetMSMode( MSMode mode, bool block, int *cn )
{
	CommandReceiver cr(this,CommandSetMSMode);
	if ( !sendCommand( CommandSetMSMode, QByteArray( (const char *)&mode, 1 ) ) )
		return false;
	if( !block )
		return true;
	if ( !cr.waitForCommand(timeoutforcommands) )
		return false;
	if ( mode != (unsigned char)cr.data.at(0) )
		return false;
	if ( cn )
		*cn = cr.cn;
	return true;
}

bool SiProto::trySettingsDevice( void )
{
	QSettings set;
	if ( set.contains( "siproto/dev" ) ) {
		QString sdev = set.value("siproto/dev").toString();
		if ( sdev.isEmpty() )
			return false;
		if ( tryDevice( sdev ) )
			return true;
	}
	return false;
}

void SiProto::setFeedbackEnabled( bool enabled )
{
	autoAccept = enabled;
}

bool SiProto::searchAndOpen( void )
{
	QSettings set;
	if ( set.contains( "siproto/dev" ) ) {
		if ( tryDevice( set.value( "siproto/dev" ).toString() ) )
			return true;
	}
	QStringList dl = fullDeviceList();
	for( int i=0;i<dl.count();i++ ) {
		if ( i== 1 || i == 2)
			continue;
		if ( tryDevice( dl.at(i) ) ) {
			if ( !QApplication::applicationName().isEmpty() )
				set.setValue( "siproto/dev", dl.at(i) );
			return true;
		}
	}
	return false;
}

bool SiProto::StartGetBackup( int startaddr, int size )
{
	backupreadpointer = startaddr;
	if ( size>0 )
		backupreadendaddr = startaddr+size;
	else
		backupreadendaddr = 0;
	startingbackup = true;
	return GetSystemValue( 0, 0x80 );
}

bool SiProto::StartGetPunchBackupData()
{
	readingpunchbackup = true;
	backupreadpointer = 0x100;
	return GetSystemValue( 0, 0x80 );
}

bool SiProto::StartGetCardBackupData()
{
	readingcardbackup = true;
	backupreadpointer = 0x100;
	card6blocksread = 0;
	return GetSystemValue( 0, 0x80 );
}

void SiProto::handlePunchBackupData(unsigned int /*addr*/, const QByteArray &data, int cn)
{
	unsigned char *b = (unsigned char *)data.data();
	//	int total = (lastreadinfo.backupmemaddr-0x100)/lastreadinfo.backuprecordsize;
	//int blockfirst = (addr-0x100)/lastreadinfo.backuprecordsize;
	for( int i=0;i<data.length()/lastreadinfo.backuprecordsize;i++ ) {
		unsigned char *d = b+(i*lastreadinfo.backuprecordsize);
		PunchBackupData pbd( d, lastreadinfo.backuprecordsize, lastreadinfo.swversion, cn );
		emit backupPunch( pbd );
	}
}

void SiProto::resolveCard6Backup(QList<SiCard> *clist)
{
	card6forread.resolveBackupBlocks( card6backupblocks );
	if ( clist ) {
		clist->append( card6forread );
	}
	emit backupCard(&card6forread);
	card6blocksread = 0;
}

void SiProto::handleCardBackupData(unsigned int /*addr*/, const QByteArray &data, QList<SiCard> *clist )
{
	const unsigned char *b = (const unsigned char *)data.data();
	if ( data.length() != 128 ) {
		qWarning( "Could not get 128 bytes of backup data" );
		return;
	}
	if ( data.at(30) == 0x00 && data.at(31) == 0x07 ) {
		if ( card6blocksread )
			resolveCard6Backup(clist);
		SiCard5 card( data );
		emit backupCard(&card);
		if ( clist )
			clist->append( card );
		return;
	} else if ( b[4] == 0xED && b[5] == 0xED && b[6] == 0xED && b[7] == 0xED  ) {
		if ( card6blocksread )
			resolveCard6Backup(clist);
		card6forread.reset();
		card6forread.addBlock(0, data);
		card6blocksread = 1;
		card6backupblocks.clear();
		return;
	} else if ( b[4] == 0xEA && b[5] == 0xEA && b[6] == 0xEA && b[7] == 0xEA  ) {
		if ( card6blocksread )
			resolveCard6Backup(clist);
		qWarning( "Backup reading Card8 / Card9 / pCard not impmlemented" );
	}
	if ( card6blocksread ) {
		card6backupblocks.append( data );
		if ( card6backupblocks.count() == 6 )
			resolveCard6Backup(clist);
	}
}

QList<PunchBackupData> SiProto::GetPunchBackupData()
{
	QList<PunchBackupData> resp;
	QByteArray ba;
	if ( !GetSystemValue( SiProto::BackupMemoryAddres, 0x07, &ba ) )
		return resp;
	unsigned int bmem = 0;
	bmem = ((unsigned char)ba.at(0))<<24;
	bmem |= ((unsigned char)ba.at(1))<<16;
	bmem |= ((unsigned char)ba.at(5))<<8;
	bmem |= ((unsigned char)ba.at(6));
	if ( !GetSystemValue( 0, 0x80, &ba ) )
		return resp;
	int recordsize;
	if ( (unsigned char)ba.at(ProtocolConf) & FlagExtendedProtocol )
		recordsize = 8;
	else
		recordsize = 6;
	unsigned char v1 = (unsigned char)ba.at(SWVersion);
	unsigned char v2 = (unsigned char)ba.at(SWVersion+1);
	unsigned char v3 = (unsigned char)ba.at(SWVersion+2);
	double sw = v1-'0'+(v2-'0')/10.+(v3-'0')/100.;
	QByteArray bdata;
	unsigned int raddr;
	unsigned int rmem = 0x100;
	while( rmem < bmem ) {
		unsigned int stilltoread = bmem-rmem;
		int cn;
		if ( !GetDataFromBackup( rmem, (stilltoread>128 ? 128 : stilltoread), &raddr, &bdata, &cn ) ) {
			sleep( 1 );
			if ( !GetDataFromBackup( rmem, (stilltoread>128 ? 128 : stilltoread), &raddr, &bdata, &cn ) )
				return resp;
		}
		rmem += ((int)(bdata.length()/recordsize))*recordsize;
		unsigned char *b = (unsigned char *)bdata.data();
		for( int i=0;i<bdata.length()/recordsize;i++ ) {
			unsigned char *d = b+(i*recordsize);
			PunchBackupData pbd(d, recordsize, sw, cn);
			resp.append(pbd);
		}

	}
	return resp;
}

QList<SiCard> SiProto::GetCardBackupData( unsigned int startaddr, int bmem)
{
	QList<SiCard> resp;
	if ( bmem == 0 ) {
		QByteArray ba;
		if ( !GetSystemValue( SiProto::BackupMemoryAddres, 0x07, &ba ) )
			return resp;
		bmem = ((unsigned char)ba.at(0))<<24;
		bmem |= ((unsigned char)ba.at(1))<<16;
		bmem |= ((unsigned char)ba.at(5))<<8;
		bmem |= ((unsigned char)ba.at(6));
	}
	
	unsigned int raddr;
	QByteArray bdata;
	card6blocksread = 0;
	for ( int j=startaddr;j<bmem;j+=128 ) {
		if ( GetDataFromBackup( j, 128, &raddr, &bdata ) ) {
			if ( bdata.length() != 128 ) {
				qWarning( "Could not get 128 bytes of backup data" );
				continue;
			}
			handleCardBackupData(raddr, bdata, &resp);
		}
	}
	if ( card6blocksread )
		resolveCard6Backup(&resp);
	return resp;
}

void SiProto::updateSystemInfo(unsigned char addr, const QByteArray &data)
{
	unsigned char lastaddr = data.length()+addr;
	const unsigned char *d = (const unsigned char *)data.data();
	if ( addr <= SWVersion && lastaddr >= (SWVersion+3) ) {
		unsigned char v1 = d[SWVersion-addr];
		unsigned char v2 = d[SWVersion-addr+1];
		unsigned char v3 = d[SWVersion-addr+2];
		lastreadinfo.swversion =  v1-'0'+(v2-'0')/10.+(v3-'0')/100.;
	}
	if ( addr <= CardBlocks && lastaddr >= CardBlocks )
		lastreadinfo.cardblocks = d[CardBlocks-addr];
	if ( addr <= ProtocolConf && lastaddr >= ProtocolConf ) {
		lastreadinfo.protocolconf = d[ProtocolConf-addr];
		if ( lastreadinfo.protocolconf & FlagExtendedProtocol ) {
			lastreadinfo.backuprecordsize = 8;
			lastreadinfo.backupreadsize = 128;
		} else {
			lastreadinfo.backuprecordsize = 6;
			lastreadinfo.backupreadsize = 120;
		}
	}
	if ( addr <= StationMode && lastaddr >= StationMode ) {
		lastreadinfo.stationmode = d[StationMode-addr];
		if ( lastreadinfo.stationmode == StationReadSICards ) {
			lastreadinfo.backuprecordsize = 128;
			lastreadinfo.backupreadsize = 128;
		}
	}
	if ( addr <= BackupMemoryAddres && lastaddr >= (BackupMemoryAddres+7) ) {
		const unsigned char *d2 = d+BackupMemoryAddres-addr;
		lastreadinfo.backupmemaddr = (d2[0]<<24)|(d2[1]<<16)|(d2[5]<<8)|d2[6];
	}
}

void SiProto::stopTasks()
{
	startingbackup = false;
	readingpunchbackup = false;
	readingcardbackup = false;
}

SiCard SiProto::cardFromData( const QByteArray ba )
{
	unsigned char *b = (unsigned char *)ba.data();
	if ( b[30] == 0x00 && b[31] == 0x07 )
		return SiCard5( ba );
	else if ( b[4] == 0xED && b[5] == 0xED && b[6] == 0xED && b[7] == 0xED  )
		return SiCard6( ba );
	else if ( b[4] == 0xEA && b[5] == 0xEA && b[6] == 0xEA && b[7] == 0xEA  )
		qWarning( "Creating Card8 / Card9 / pCard from bytearray not impmlemented" );
	return SiCard();
}

bool SiProto::ResetBackup( int *cn  )
{
	CommandReceiver cr( this, CommandEraseBackupData );

	if ( !sendCommand( CommandEraseBackupData ) )
		return false;

	if ( !cr.waitForCommand(timeoutforcommands) )
		return false;
	if ( cn )
		*cn = cr.cn;
	return true;
}

bool SiProto::ResetBackup()
{
	return sendCommand( CommandEraseBackupData );
}
