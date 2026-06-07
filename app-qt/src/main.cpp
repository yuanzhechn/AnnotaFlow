#include "MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("AnnotaFlow");
    QApplication::setOrganizationName("AnnotaFlow");
    QApplication::setApplicationVersion("0.3.3");

    MainWindow window;
    window.show();

    return app.exec();
}
