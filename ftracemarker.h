#pragma once

#include <QObject>
#include <QFile>
#include <kwinglobals.h>

namespace KWin
{

class KWIN_EXPORT FTraceLogger : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.FTrace");
public:
    ~FTraceLogger() = default;
    /**
     * enabled through DBus and logging started
     */
    bool isActive();

    /**
     * Log a message to a
     */
    void log(const QByteArray &message);

public Q_SLOTS:
    Q_SCRIPTABLE void setEnabled(bool enabled);

private:
    bool open();
    QFile m_file;
    KWIN_SINGLETON(FTraceLogger)
};

class FTraceTrackDuration {
public:
    FTraceTrackDuration(const char *message);
    ~FTraceTrackDuration();
private:
    QByteArray m_message;
    qulonglong m_ctx;
};

}
