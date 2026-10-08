#include <QApplication>
#include <QIcon>
#include <QtCore/QTimer>
#include <QCommandLineParser>
#include <iostream>
#include <cstring>
#include "Logging.h"
#include "Version.h"


#include <QFile>
#include <QTextStream>

#include "ui/MainWindow.h"
#include "core/SwitcherEngine.h"
#include "core/AmcpClient.h"
#include "core/PanelProtocol.h"
#include "config/Configuration.h"

int main(int argc, char* argv[])
{
    // Help and version must work without a display or a running CasparCG server.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::cout << "Usage: kavtor [options]\n\n"
                         "CasparCG engine preparation and management.\n"
                         "  -h, --help     Show this help and exit\n"
                         "  --version      Show software version and exit\n"
                         "  --verbose      Enable runtime protocol diagnostics\n"
                         "  --connect      Connect and prepare the configured engine on startup\n\n"
                         "Prepare sources and endpoints in the management workspace.\n"
                         "Configuration is stored under the Qt user configuration directory.\n";
            return 0;
        }
        if (std::strcmp(argv[i], "--version") == 0) {
            std::cout << "kavtor " << KAVTOR_VERSION << '\n';
            return 0;
        }
    }
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/kavtor.svg")));
    app.setApplicationVersion(QStringLiteral(KAVTOR_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("CasparCG engine manager"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("connect"), QStringLiteral("Connect and prepare the configured engine on startup")});
    parser.addOption({QStringLiteral("verbose"), QStringLiteral("Enable runtime protocol diagnostics")});
    parser.process(app);
    if (parser.isSet(QStringLiteral("verbose")))
        QLoggingCategory::setFilterRules(QStringLiteral("kavtor.runtime.debug=true"));
    app.setApplicationName(QStringLiteral("kavtor"));
    app.setOrganizationName(QStringLiteral("kavtor"));

    QFile styleFile(QStringLiteral(":/styles/styles.css"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&styleFile);
        app.setStyleSheet(stream.readAll());
    }

    auto* configuration = new Configuration(&app);
    configuration->load();

    auto* amcp = new AmcpClient(&app);
    auto* engine = new SwitcherEngine(configuration, amcp, &app);
    auto* panel = new PanelProtocol(engine, &app);
    panel->start(static_cast<quint16>(configuration->panelPort()));

    MainWindow window(engine, panel);
    window.show();
    if(parser.isSet(QStringLiteral("connect"))) QTimer::singleShot(0, engine, &SwitcherEngine::connectToCaspar);
    return app.exec();
}
