
#include <QApplication>
#include <QThread>

#include <siproto.h>

#include "dialog.h"

int main( int argc, char *argv[] )
{
	QApplication app( argc, argv );

	QCoreApplication::setApplicationName( "qsiapp" );

	Dialog dia;
	dia.show();

	return app.exec();

}
