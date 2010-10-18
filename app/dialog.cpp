#include "dialog.h"
#include "ui_dialog.h"

#include <QStatusBar>
#include <QStandardItemModel>
#include <QFileDialog>
#include <QMessageBox>

class commandWrapper {
	public:
		commandWrapper( SiProto *si, bool settoslave, QProgressBar *bar, int plannedcommandcount );
		~commandWrapper();

	private:
		bool inslavemode;
		SiProto *si;
		QProgressBar *progressbar;
};
commandWrapper::commandWrapper( SiProto *sip, bool settoslave, QProgressBar *bar, int plannedcommandcount ) :
		inslavemode( settoslave ),
		si( sip ),
		progressbar( bar )
{
	plannedcommandcount *= 2;
	if ( settoslave )
		plannedcommandcount += 4;
	progressbar->setMaximum(plannedcommandcount);
	progressbar->setValue(0);
	progressbar->setStyleSheet("");

	if ( settoslave )
		si->SetMSMode(SiProto::SlaveCommunication);
}

commandWrapper::~commandWrapper()
{
	if ( inslavemode )
		si->SetMSMode(SiProto::DirectCommunication);
	if ( progressbar->value() != progressbar->maximum() ) {
		progressbar->setStyleSheet("QProgressBar { border: 2px solid grey; border-radius: 5px; } QProgressBar::chunk { background-color: rgb(255, 0, 0);}" );
	}
}

Dialog::Dialog(QWidget *parent) :
    QDialog(parent),
	ui(new Ui::Dialog),
	inslavemode( false ),
	currenttask( taskNone ),
	cancelButton( NULL )
{
	ui->setupUi(this);


	ui->progressBar->setMaximum(1);
	ui->progressBar->setValue(1);

	ui->operatingMode->addItem("Control", SiProto::StationControl );
	ui->operatingMode->addItem("Start", SiProto::StationStart);
	ui->operatingMode->addItem("Finish", SiProto::StationFinish);
	ui->operatingMode->addItem("Read SI cards", SiProto::StationReadSICards);
	ui->operatingMode->addItem("Clear", SiProto::StationClear);
	ui->operatingMode->addItem("Check", SiProto::StationCheck);
	bar = new QStatusBar( this );
	ui->verticalLayout->addWidget( bar );
	bar->setSizeGripEnabled(false);

	sicardmodel = new QStandardItemModel(this);
	ui->siCards->setModel( sicardmodel );
	backupmodel = new QStandardItemModel(this);
	ui->stationBackupView->setModel( backupmodel );

	connect( &si, SIGNAL(statusMessage( const QString & ) ), SLOT(siStatusMsg(QString)) );
	connect( &si, SIGNAL(cardRead(const SiCard &)), SLOT(siCardRead(SiCard)) );
	connect( &si, SIGNAL(backupCard(const SiCard*)), SLOT(gotBackupSiCard(const SiCard*)) );
	connect( &si, SIGNAL(backupPunch(PunchBackupData)), SLOT(gotBackupPunch(PunchBackupData)));
	connect( &si, SIGNAL(gotCommand(unsigned char,QByteArray,int)), SLOT(updateProgressBar()) );
	connect( &si, SIGNAL(sentCommand(unsigned char,QByteArray)), SLOT(updateProgressBar()) );
	connect( &si, SIGNAL(backupBlocmNumFrom(int,int)), SLOT(readBackupBlock(int,int)) );

	connect( &si, SIGNAL(gotMSMode(SiProto::MSMode,int)), SLOT(gotMSMode(SiProto::MSMode)));
	connect( &si, SIGNAL(gotTime(QDateTime,QDateTime,int)), SLOT(gotTime(QDateTime,QDateTime)) );
	connect( &si, SIGNAL(gotSetTime(QDateTime,int)), SLOT(gotSetTime(QDateTime)) );
	connect( &si, SIGNAL(gotNAK()), SLOT(stopTask()) );
	connect( &si, SIGNAL(gotSystemValue(unsigned char,QByteArray,int)), SLOT(gotSystemValu(unsigned char,QByteArray)) );
	connect( &si, SIGNAL(gotSetSystemValue(unsigned char,QByteArray,int)), SLOT(gotSetSystemValu(unsigned char,QByteArray)) );
	connect( &si, SIGNAL(gotErasedBackup()), SLOT(stopTask()) );
	fillCardBlocksCombo();

	connect( ui->buttonBox, SIGNAL(clicked(QAbstractButton*)), SLOT(buttonClicked(QAbstractButton*)) );
	if ( !si.searchAndOpen() )
		qWarning( "Failed to open" );

}

Dialog::~Dialog()
{
    delete ui;
}

void Dialog::buttonClicked(QAbstractButton *b)
{
	if ( b == cancelButton ) {
		stopTask();
	} else if ( b == ui->buttonBox->button(QDialogButtonBox::Close) )
		close();
}
void Dialog::updateProgressBar()
{
	ui->progressBar->setValue(ui->progressBar->value()+1);
}

void Dialog::fillCardBlocksCombo(unsigned int val)
{
	if ( ui->CardBlocks->count() == 2 &&
		 ( val == 0xFF || val == 0xC1 ) )
		return;
	ui->CardBlocks->clear();
	ui->CardBlocks->addItem("All 8 data blocks", 0xFF);
	ui->CardBlocks->addItem("Blocks 0,6,7", 0xC1);
	if ( val == 0x00 )
		ui->CardBlocks->addItem("None");
	else if ( val != 0xFF && val != 0xC1 ) {
		QStringList bnums;
		int num = 0;
		unsigned int tmp = val;
		while( tmp ) {
			if ( tmp&0x01 )
				bnums.append(QString::number(num));
			num++;
			tmp = tmp >> 1;
		}
		ui->CardBlocks->addItem("Blocks "+bnums.join(","), val);
	}
}

void Dialog::gotSetSystemValu(unsigned char addr, const QByteArray &/*data*/)
{
	if ( addr == 0x70 ) {
		unsigned char cval = ui->CardBlocks->itemData(ui->CardBlocks->currentIndex()).toInt();
		if ( ((unsigned char)latestconf[SiProto::CardBlocks]) == cval ) {
			ui->progressBar->setValue(ui->progressBar->maximum());
			stopTask();
			return;
		}
		if ( !inslavemode && (unsigned char)latestconf[SiProto::CardBlocks] != cval ) {
			QByteArray ba;
			ba.append(cval);
			if ( si.SetSystemValue(SiProto::CardBlocks, ba) )
				return;
		}
	}
	stopTask();
}

void Dialog::gotSystemValu(unsigned char addr, const QByteArray &ba)
{
	if ( addr == 0x00 && ba.length() == 0x80)
		latestconf = ba;
	if ( currenttask == taskReadConf ) {
		gotReadConf( addr, ba );
		stopTask();
	} else if ( currenttask == taskWriteConf ) {
		writeConfEnd();
	}
}


// Write configuration data on 0x70-0x80
void Dialog::writeConfEnd()
{
	latestconf[SiProto::StationCode] = ui->stationCode->value();
	unsigned char protomode = 0;
	if ( ui->extendedProtocol->isChecked() )
		protomode |= (SiProto::FlagExtendedProtocol | SiProto::FlagHandshake);
	if ( ui->autoSend->isChecked() )
		protomode |= SiProto::FlagAutoSendOut;
	int stmode = ui->operatingMode->itemData(ui->operatingMode->currentIndex()).toInt();
	if ( stmode == SiProto::StationReadSICards )
		protomode |= SiProto::FlagHandshake;
	latestconf[SiProto::ProtocolConf] = protomode;
	latestconf[SiProto::StationMode] = stmode;
	if ( !si.SetSystemValue(0x70, latestconf.mid(0x70,10)) )
		stopTask();
	return;
}


void Dialog::gotReadConf(unsigned char addr, const QByteArray &ba)
{
	unsigned char *d = (unsigned char *)ba.data();
	if ( ba.length() >= (SiProto::StationCode-addr) )
	   ui->stationCode->setValue(d[SiProto::StationCode-addr]);
	if ( ba.length() >= (SiProto::ProtocolConf-addr) ) {
		unsigned char protomode = d[SiProto::ProtocolConf-addr];
		if ( protomode & SiProto::FlagExtendedProtocol )
			ui->extendedProtocol->setChecked(true);
		if ( protomode & SiProto::FlagAutoSendOut )
			ui->autoSend->setChecked(true);
	}
	if ( ba.length() >= (SiProto::StationMode-addr) ) {
		unsigned char omode = d[SiProto::StationMode-addr];
		for( int i=0;i<ui->operatingMode->count();i++ ) {
			if ( omode != ui->operatingMode->itemData(i).toInt() )
				continue;
			ui->operatingMode->setCurrentIndex(i);
			break;
		}
	}
	if ( ba.length() >= (SiProto::CardBlocks-addr) ) {
		unsigned int cblocks = d[SiProto::CardBlocks-0x00];
		fillCardBlocksCombo(cblocks);
		for( int i=0;i<ui->CardBlocks->count();i++ ) {
			if ( ui->CardBlocks->itemData(i) == cblocks ) {
				ui->CardBlocks->setCurrentIndex(i);
				break;
			}
		}
	}
}

void Dialog::gotSetTime( const QDateTime &/*dt*/ )
{
	--settimecount;
	gettimecount = ui->readNumber->value();
	timediffcount = 0;
	timediffsum = 0;
	if ( !si.GetTime() )
		stopTask();
}

void Dialog::gotTime( const QDateTime &dt, const QDateTime &ct )
{
	QString tformat = "dd/MM/yy hh:mm:ss.zzz";
	ui->computerTime->setText(ct.toString(tformat));
	ui->deviceTime->setText(dt.toString(tformat));
	int diff = ct.time().msecsTo(dt.time());
	timediffsum += diff;
	if ( timediffcount++ == 0 )
		timemindiff = timemaxdiff = diff;
	else {
		if ( qAbs(timemindiff)>qAbs(diff) )
			timemindiff = diff;
		if ( qAbs(timemaxdiff)<qAbs(diff) )
			timemaxdiff = diff;
	}
	ui->minDiff->setText( QString::number(timemindiff) );
	ui->maxDiff->setText( QString::number(timemaxdiff) );
	ui->averageDiff->setText(QString::number(timediffsum/timediffcount));
	if ( !(--gettimecount) || !si.GetTime() ) {
		if ( currenttask == taskGetTime ) {
		  stopTask();
		  return;
		} else if ( currenttask == taskSetTime ) {
			if ( qAbs(ui->maxAverageDiff->value()) > qAbs(ui->averageDiff->text().toInt()) ) {
				ui->progressBar->setValue(ui->progressBar->maximum());
				stopTask();
				return;
			}
			if ( settimecount <= 0 ) {
				stopTask();
				return;
			}
			ui->progressBar->setMaximum(ui->progressBar->maximum()+(ui->readNumber->value()+1)*2+1);
			QDateTime sdt = QDateTime::currentDateTime();
			settimefix -= ui->averageDiff->text().toInt();
			sdt = sdt.addMSecs(settimefix);
			if ( !si.SetTime(sdt) )
				stopTask();
		}
	}
}

void Dialog::gotMSMode( SiProto::MSMode )
{
	if ( currenttask == taskGetTime ) {
		gettimecount = ui->readNumber->value();
		timediffcount = 0;
		timediffsum = 0;
		if ( !si.GetTime() )
			stopTask();
	} else if ( currenttask == taskSetTime ) {
		settimecount = ui->writeTrys->value();
		if ( !si.SetTime( QDateTime::currentDateTime() ) )
			stopTask();
	} else if ( currenttask == taskReadConf ) {
		if ( !si.GetSystemValue(0x00,0x80) )
			stopTask();
	} else if ( currenttask == taskWriteConf ) {
		if ( !si.GetSystemValue(0x00,0x80) )
			stopTask();
	} else if ( currenttask == taskEraseBackup ) {
		if ( !si.ResetBackup() )
			stopTask();
	} else if ( currenttask == taskGetBackup ) {
		if ( !si.StartGetBackup() )
			stopTask();
	} else if ( currenttask == taskNone ) {
		if ( ui->progressBar->value() < ui->progressBar->maximum() ) {
			ui->progressBar->setStyleSheet("QProgressBar { border: 2px solid grey; border-radius: 5px; } QProgressBar::chunk { background-color: rgb(255, 0, 0);}" );
		}
	}
}

void Dialog::stopTask()
{
	si.stopTasks();
	enableButtons(true);
	currenttask = taskNone;
	if ( inslavemode ) {
		si.SetMSMode(SiProto::DirectCommunication, false);
		inslavemode = false;
	}
}

void Dialog::enableButtons(bool enable)
{
	ui->getTimeButton->setEnabled(enable);
	ui->setTimeButton->setEnabled(enable);
	//ui->tabWidget->setEnabled(enable);
	ui->MSMode->setEnabled(enable);
	ui->readStationConf->setEnabled(enable);
	ui->writeStationConf->setEnabled(enable);
	ui->stationCode->setEnabled(enable);
	ui->workingTim->setEnabled(enable);
	ui->operatingMode->setEnabled(enable);
	ui->protocolConfBox->setEnabled(enable);
	ui->readBackup->setEnabled(enable);
	ui->clearBackup->setEnabled(enable);
	ui->saveStationBackup->setEnabled(enable);
	if ( enable ) {
		if ( ui->operatingMode->itemData(ui->operatingMode->currentIndex()) == SiProto::StationReadSICards )
		  ui->CardBlocks->setEnabled(enable);
	} else
		ui->CardBlocks->setEnabled(enable);

	if ( enable ) {
		if ( cancelButton )
		  ui->buttonBox->removeButton(cancelButton);
		cancelButton = NULL;
	} else {
		cancelButton = ui->buttonBox->addButton(QDialogButtonBox::Cancel);
	}
}

void Dialog::startTask( tasks t, int commands )
{
	enableButtons( false );
	currenttask = t;
	commands = commands*2;
	if ( ui->MSMode->currentIndex() )
		commands += 4;
	ui->progressBar->setMaximum( commands );
	ui->progressBar->setValue(0);
	ui->progressBar->setStyleSheet("");
	if ( ui->MSMode->currentIndex() ) {
		inslavemode = true;
		si.SetMSMode(SiProto::SlaveCommunication, false);
	} else
		gotMSMode( SiProto::DirectCommunication );
}

void Dialog::on_getTimeButton_clicked()
{
	int c = ui->readNumber->value();
	startTask( taskGetTime, c );
}

void Dialog::doGetTime( int count )
{
	QDateTime dt, ct;
	QString tformat = "dd/MM/yy hh:mm:ss.zzz";
	int mindiff, maxdiff;
	int diffsum=0;
	for( int i=0;i<count;i++ ) {
		ct = QDateTime::currentDateTime();
		if ( !si.GetTime(&dt) ) {
			QPalette pal;
			pal.setColor(QPalette::Base,Qt::red);
			pal.setColor(QPalette::Text, Qt::red);
			ui->deviceTime->setPalette(pal);
			continue;
		}
		int diff = ct.time().msecsTo(dt.time());
		diffsum += diff;
		ui->computerTime->setText(ct.toString(tformat));
		ui->deviceTime->setText(dt.toString(tformat));
		if ( i==0 ) {
			mindiff = maxdiff = diff;
		} else {
			if ( qAbs(mindiff)>qAbs(diff) )
				mindiff = diff;
			if ( qAbs(maxdiff)<qAbs(diff) )
				maxdiff = diff;
		}
		ui->minDiff->setText( QString::number(mindiff) );
		ui->maxDiff->setText( QString::number(maxdiff) );
		ui->averageDiff->setText(QString::number(diffsum/(i+1)));
	}
}

void Dialog::on_setTimeButton_clicked()
{
	int c = ui->readNumber->value();
	settimefix = 0;
	startTask( taskSetTime, c+1 );
}

void Dialog::on_readStationConf_clicked()
{
	ui->extendedProtocol->setChecked(false);
	ui->autoSend->setChecked(false);
	ui->stationCode->setValue(0);
	ui->operatingMode->setCurrentIndex(0);

	startTask( taskReadConf, 1);
	return;

	commandWrapper cw( &si, ui->MSMode->currentIndex(), ui->progressBar, 1 );
	Q_UNUSED( cw );

	QByteArray ba;
	if ( !si.GetSystemValue(0x00,0x80,&ba) )
		return;
	ui->stationCode->setValue((unsigned char )ba.at(SiProto::StationCode));
	unsigned char protomode = (unsigned char)ba.at(SiProto::ProtocolConf);
	if ( protomode & SiProto::FlagExtendedProtocol )
		ui->extendedProtocol->setChecked(true);
	if ( protomode & SiProto::FlagAutoSendOut )
		ui->autoSend->setChecked(true);
	unsigned char omode = ba.at(SiProto::StationMode);
	for( int i=0;i<ui->operatingMode->count();i++ ) {
		if ( omode != ui->operatingMode->itemData(i).toInt() )
			continue;
		ui->operatingMode->setCurrentIndex(i);
		break;
	}
	unsigned int cblocks = (unsigned char)ba.at(SiProto::CardBlocks);
	fillCardBlocksCombo(cblocks);
	for( int i=0;i<ui->CardBlocks->count();i++ ) {
		if ( ui->CardBlocks->itemData(i) == cblocks ) {
			ui->CardBlocks->setCurrentIndex(i);
			break;
		}
	}
}

void Dialog::on_writeStationConf_clicked()
{
	if ( !ui->MSMode->currentIndex() ) {
		if ( QMessageBox::warning(this, "Writing read station conf", "Are you sure you want to rewrite the configuration of reader station?", QMessageBox::Yes, QMessageBox::No) == QMessageBox::No )
			return;
	}
	startTask( taskWriteConf, 3);
	return;

	commandWrapper cw( &si, ui->MSMode->currentIndex(), ui->progressBar, 3 );
	Q_UNUSED( cw );

	QByteArray ba;
	int maddr = 0x70;
	if ( !si.GetSystemValue(maddr,0x10,&ba) )
		return;
	ba[SiProto::StationCode-maddr] = ui->stationCode->value();
	unsigned char protomode = 0;
	if ( ui->extendedProtocol->isChecked() )
		protomode |= (SiProto::FlagExtendedProtocol | SiProto::FlagHandshake);
	if ( ui->autoSend->isChecked() )
		protomode |= SiProto::FlagAutoSendOut;
	int stmode = ui->operatingMode->itemData(ui->operatingMode->currentIndex()).toInt();
	if ( stmode == SiProto::StationReadSICards )
		protomode |= SiProto::FlagHandshake;
	ba[SiProto::ProtocolConf-maddr] = protomode;
	ba[SiProto::StationMode-maddr] = stmode;
	if ( !si.SetSystemValue(maddr, ba ) )
		return;
	ba.clear();
	ba.append(ui->CardBlocks->itemData(ui->CardBlocks->currentIndex()).toInt());
	if ( !si.SetSystemValue(SiProto::CardBlocks, ba) )
		return;
}

void Dialog::siStatusMsg(const QString &msg)
{
	bar->showMessage(msg, 3000);
}

void Dialog::on_clearSICards_clicked()
{
   sicardmodel->clear();
}

void Dialog::on_clearBackup_clicked()
{
   backupmodel->clear();
}

void Dialog::siCardRead(const SiCard &card)
{
	QList<QStandardItem*> rd;
	rd.append(new QStandardItem(QString("%0").arg(card.getCardNumber())) );
	QList<PunchingRecord> plist = card.getPunches();
	for( int i=0;i<plist.count();i++ ) {
		rd.append(new QStandardItem(plist.at(i).time.toString()));
	}
	sicardmodel->appendRow(rd);
}

void Dialog::on_saveSICards_clicked()
{
   saveModelAsCSV(ui->siCards->model());
}

void Dialog::on_saveStationBackup_clicked()
{
   saveModelAsCSV(backupmodel);
}

void Dialog::saveModelAsCSV(QAbstractItemModel *model)
{
	QFileDialog fdia;
	QStringList filters;
	filters.append("CSV files (*.csv)");
	filters.append("All files (*)");
	fdia.setNameFilters(filters);
	fdia.setFileMode(QFileDialog::AnyFile);
	fdia.setAcceptMode(QFileDialog::AcceptSave);
	fdia.setDefaultSuffix(".csv");
	if( !fdia.exec() || fdia.selectedFiles().count() == 0 )
		return;
	QString fname = fdia.selectedFiles().at(0);
	QFile f( fname );
	if ( !f.open(QFile::WriteOnly) ) {
		qWarning( "Could not open file for writing" );
		return;
	}
	QStringList il;
	for( int i=0;i<model->rowCount();i++ ) {
		il.clear();
		for( int j=0;j<model->columnCount();j++ ) {
			il.append(model->index(i,j).data().toString());
		}
		f.write((il.join(";")+"\n").toUtf8());
	}
	f.close();
}

void Dialog::on_readBackup_clicked()
{
	startTask( taskGetBackup, 1 ); // TODO commmand number vale.
	return;
	// TODO continue
	si.StartGetPunchBackupData();
	return;
	// TODO set progress number here
	commandWrapper cw( &si, ui->MSMode->currentIndex(), ui->progressBar, 2 );
	Q_UNUSED( cw );

	backupmodel->clear();
	QByteArray ba;
	if ( !si.GetSystemValue(SiProto::StationMode, 1, &ba) )
		return;
	if ( ba.length() < 1 )
		return;
	if ( ((unsigned char)ba.at(0)) == SiProto::StationReadSICards ) {
		QList<SiCard*> clist = si.GetCardBackupData();
		return;
		QList<QStandardItem*> rd;
		for( int i=0;i<clist.count();i++ ) {
			SiCard *card = clist.at(i);
			rd.clear();
			rd.append(new QStandardItem(QString("%0").arg(card->getCardNumber())) );
			QList<PunchingRecord> plist = card->getPunches();
			for( int i=0;i<plist.count();i++ ) {
				rd.append(new QStandardItem(plist.at(i).time.toString()));
			}
			backupmodel->appendRow(rd);
		}
	} else {
		QList<PunchBackupData> pdlist = si.GetPunchBackupData();
		QList<QStandardItem*> rd;
		for( int i=0;i<pdlist.count();i++ ) {
			rd.clear();
			rd.append(new QStandardItem(QString("%0").arg(pdlist.at(i).cardnum)));
			rd.append(new QStandardItem(QString("%0").arg(pdlist.at(i).t.toString())));
			backupmodel->appendRow(rd);
		}
	}
}

void Dialog::readBackupBlock(int num, int total)
{
	bar->showMessage(QString("Read backup block %0 from %1").arg(num).arg(total));
	ui->progressBar->setMaximum(total);
	ui->progressBar->setValue(num);
	if ( currenttask == taskGetBackup && num == total )
		stopTask();
}

void Dialog::gotBackupPunch(const PunchBackupData &pd)
{
	QList<QStandardItem*> rd;
	rd.append(new QStandardItem(QString("%0").arg(pd.cn)));
	rd.append(new QStandardItem(QString("%0").arg(pd.cardnum)));
	QString timestr;
	if ( pd.d.isValid() ) {
		timestr = QDateTime( pd.d, pd.t ).toString();
	} else
		timestr = pd.t.toString();
	rd.append(new QStandardItem(QString("%0").arg(timestr)));
	backupmodel->appendRow(rd);
	ui->stationBackupView->scrollTo(backupmodel->index(backupmodel->rowCount()-1,0));
}

void Dialog::gotBackupSiCard(const SiCard *card)
{
	QList<QStandardItem*> rd;
	rd.clear();
	rd.append(new QStandardItem(QString("%0").arg(card->getCardNumber())) );
	QList<PunchingRecord> plist = card->getPunches();
	for( int i=0;i<plist.count();i++ ) {
		rd.append(new QStandardItem(plist.at(i).time.toString()));
	}
	backupmodel->appendRow(rd);
	ui->stationBackupView->scrollTo(backupmodel->index(backupmodel->rowCount()-1,0));
}



void Dialog::on_operatingMode_activated(int /*index*/)
{
}

void Dialog::on_operatingMode_currentIndexChanged(int index)
{
   if ( ui->operatingMode->itemData(index) == SiProto::StationReadSICards )
	   ui->CardBlocks->setEnabled(true);
   else
	   ui->CardBlocks->setEnabled(false);
}
void Dialog::on_resetBackup_clicked()
{
	if ( QMessageBox::warning(this, "Reset backup pointer", "Are you sure you want clear backup?", QMessageBox::Yes, QMessageBox::No) == QMessageBox::No )
		return;
	startTask(taskEraseBackup, 1);
}
