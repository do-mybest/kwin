#include "ftracemarker.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

#include <QDBusConnection>

namespace KWin {

KWIN_SINGLETON_FACTORY(KWin::FTraceLogger)

FTraceLogger::FTraceLogger(QObject *parent)
    : QObject(parent)
{
    if (qEnvironmentVariableIsSet("KWIN_PERF_FTRACE")) {
        setEnabled(true);
    } else {
        QDBusConnection::sessionBus().registerObject(QStringLiteral("/FTrace"), this, QDBusConnection::ExportScriptableSlots);
    }
}

bool FTraceLogger::isActive()
{
    return m_file.isOpen();
}

void FTraceLogger::log(const QByteArray &message)
{
    if (!m_file.isOpen()) {
        return;
    }
    m_file.write(message);
    m_file.flush();
}

void FTraceLogger::setEnabled(bool enabled)
{
    if (enabled) {
        open();
    } else {
        m_file.close();
    }
}

bool FTraceLogger::open()
{
    QFile mountsFile("/proc/mounts");
    if (!mountsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "No access to mounts file. Can not determine trace marker file location.";
        return false;
    }

    auto lineInfo = [](const QString &line) {
        const int start = line.indexOf(' ') + 1;
        const int end = line.indexOf(' ', start);
        const QString dirPath(line.mid(start, end - start));
        if (dirPath.isEmpty() || !QFileInfo(dirPath).exists()) {
            return QFileInfo();
        }
        return QFileInfo(QDir(dirPath), QStringLiteral("trace_marker"));
    };
    QFileInfo markerFileInfo;
    QTextStream mountsIn(&mountsFile);
    QString mountsLine = mountsIn.readLine();

    while (!mountsLine.isNull()) {
        if (mountsLine.startsWith("tracefs")) {
            const auto info = lineInfo(mountsLine);
            if (info.exists()) {
                markerFileInfo = info;
                break;
            }
        }
        if (mountsLine.startsWith("debugfs")) {
            markerFileInfo = lineInfo(mountsLine);
        }
        mountsLine = mountsIn.readLine();
    }
    mountsFile.close();
    if (!markerFileInfo.exists()) {
        qWarning() << "Could not determine trace marker file location from mounts.";
        return false;
    }

    const QString path = markerFileInfo.absoluteFilePath();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly)) {
        qWarning() << "No access to trace marker file at:" << path;
    }
    return true;
}

static qulonglong s_context = 0;

FTraceTrackDuration::FTraceTrackDuration(const char *message)
    : m_ctx(s_context++)
{
    if (!FTraceLogger::self()->isActive()) {
        return;
    }
    m_message = message;
    FTraceLogger::self()->log(m_message + QByteArrayLiteral(" begin_ctx=") + QByteArray::number(m_ctx));
}

FTraceTrackDuration::~FTraceTrackDuration()
{
    if (!FTraceLogger::self()->isActive()) {
        return;
    }
    FTraceLogger::self()->log(m_message + QByteArrayLiteral(" end_ctx=") + QByteArray::number(m_ctx));
}

}
