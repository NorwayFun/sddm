/***************************************************************************
* Copyright (c) 2013 Abdurrahman AVCI <abdurrahmanavci@gmail.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the
* Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
***************************************************************************/

#include "Application.h"

#include "Configuration.h"
#include "Cookie.h"
#include "DisplayManager.h"
#include "LockFile.h"
#include "PowerManager.h"
#include "ScreenModel.h"
#include "SessionManager.h"
#include "SessionModel.h"
#include "ThemeConfig.h"
#include "ThemeMetadata.h"
#include "UserModel.h"
#include "Util.h"

#include <QDebug>
#include <QSettings>
#include <QVariantMap>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <QGuiApplication>
#include <QQuickView>
#include <QQmlContext>
#else
#include <QApplication>
#include <QDeclarativeView>
#include <QDeclarativeContext>
#endif

namespace SDE {
    class ApplicationPrivate {
    public:
        ApplicationPrivate() {
        }

        ~ApplicationPrivate() {
            delete configuration;
        }

        Configuration *configuration { nullptr };
        QStringList arguments;
        int argc { 0 };
        char **argv { nullptr };
    };

    Application::Application(int argc, char **argv) : d(new ApplicationPrivate()) {
        d->argc = argc;
        d->argv = argv;
        // convert arguments
        for (int i = 1; i < argc; ++i)
            d->arguments << QString(argv[i]);
    }

    Application::~Application() {
        delete d;
    }

    const QStringList &Application::arguments() const {
        return d->arguments;
    }

    void Application::init(const QString &config) {
        d->configuration = new Configuration(config);
    }

    void Application::test(const QString &theme) {
        QString themePath = theme;

        // if theme is empty, use current theme
        if (themePath.isEmpty())
            themePath = QString("%1/%2").arg(Configuration::instance()->themesDir()).arg(Configuration::instance()->currentTheme());

        // read theme metadata
        ThemeMetadata metadata(QString("%1/metadata.desktop").arg(themePath));

        // get theme main script
        QString mainScript = QString("%1/%2").arg(themePath).arg(metadata.mainScript());

        // get theme config file
        QString configFile = QString("%1/%2").arg(themePath).arg(metadata.configFile());

        // read theme config
        ThemeConfig config(configFile);

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
        // create application
        QGuiApplication app(d->argc, d->argv);
        // create view
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
#else
        // create application
        QApplication app(d->argc, d->argv);
        // create view
        QDeclarativeView view;
        view.setResizeMode(QDeclarativeView::SizeRootObjectToView);
#endif
        // create models
        SessionModel sessionModel;
        ScreenModel screenModel;
        UserModel userModel;
        // set context properties
        view.rootContext()->setContextProperty("sessionManager", nullptr);
        view.rootContext()->setContextProperty("powerManager", nullptr);
        view.rootContext()->setContextProperty("sessionModel", &sessionModel);
        view.rootContext()->setContextProperty("screenModel", &screenModel);
        view.rootContext()->setContextProperty("userModel", &userModel);
        view.rootContext()->setContextProperty("config", config);
        // load theme
        view.setSource(QUrl::fromLocalFile(mainScript));
        // show application
        view.showFullScreen();
        // execute application
        app.exec();
    }

    void Application::run() {
        // create lock file
        LockFile lock(Configuration::instance()->lockFile());
        if (!lock.success())
            return;

        bool first = true;

        while (true) {
            QString cookie = Cookie::generate();
            // reload configuration
            Configuration::instance()->load();

            // get theme path
            QString themePath = QString("%1/%2").arg(Configuration::instance()->themesDir()).arg(Configuration::instance()->currentTheme());

            // read theme metadata
            ThemeMetadata metadata(QString("%1/metadata.desktop").arg(themePath));

            // get theme main script
            QString mainScript = QString("%1/%2").arg(themePath).arg(metadata.mainScript());

            // get theme config file
            QString configFile = QString("%1/%2").arg(themePath).arg(metadata.configFile());

            // read theme config
            ThemeConfig config(configFile);

            // set DISPLAY environment variable if not set
            if (getenv("DISPLAY") == nullptr)
                setenv("DISPLAY", ":0", 1);
            // get DISPLAY
            QString display = getenv("DISPLAY");

            // set cursor theme
            setenv("XCURSOR_THEME", Configuration::instance()->cursorTheme().toStdString().c_str(), 1);

            // create display manager
            DisplayManager displayManager;
            displayManager.setDisplay(display);
            displayManager.setCookie(cookie);

            // start the display manager
            if (!displayManager.start()) {
                qCritical() << "error: could not start display manager.";
                return;
            }

            // create session manager
            SessionManager sessionManager;
            sessionManager.setDisplay(display);
            sessionManager.setCookie(cookie);

            // auto login
            if (first && !Configuration::instance()->autoUser().isEmpty()) {
                // reset flag
                first = false;
                // auto login
                sessionManager.autoLogin();
                // restart
                continue;
            }

            // reset flag
            first = false;

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
            // execute user interface in a seperate process
            // this is needed because apperantly we can't create multiple
            // QApplication instances in the same process. if this changes
            // in a future version of Qt, this workaround should be removed.
            pid_t pid = Util::execute([&] {
                // create application
                QGuiApplication app(d->argc, d->argv);
                // create view
                QQuickView view;
                view.setResizeMode(QQuickView::SizeRootObjectToView);
#else
                // create application
                QApplication app(d->argc, d->argv);
                // create view
                QDeclarativeView view;
                view.setResizeMode(QDeclarativeView::SizeRootObjectToView);
#endif
                // create power manager
                PowerManager powerManager;
                // create models
                SessionModel sessionModel;
                ScreenModel screenModel;
                UserModel userModel;
                // set context properties
                view.rootContext()->setContextProperty("sessionManager", &sessionManager);
                view.rootContext()->setContextProperty("powerManager", &powerManager);
                view.rootContext()->setContextProperty("sessionModel", &sessionModel);
                view.rootContext()->setContextProperty("screenModel", &screenModel);
                view.rootContext()->setContextProperty("userModel", &userModel);
                view.rootContext()->setContextProperty("config", config);
                // load qml file
                view.setSource(QUrl::fromLocalFile(mainScript));
                // close view on successful login
                QObject::connect(&sessionManager, SIGNAL(success()), &view, SLOT(close()));
                // show view
                view.show();
                view.setGeometry(screenModel.geometry());
                // execute application
                app.exec();
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
            });
            // wait for process to end
            Util::wait(pid);
#endif
        }
    }
}
