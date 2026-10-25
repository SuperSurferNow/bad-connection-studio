#include "app/Logging.h"

#include "core/FfmpegUtil.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QStandardPaths>

#include <cstdio>
#include <mutex>

Q_LOGGING_CATEGORY(lcFfmpeg, "bcs.ffmpeg")

namespace bcs {

namespace {

std::mutex g_mutex;
QFile* g_file = nullptr;
constexpr qint64 kMaxBytes = 1 << 20;

void rotate(const QString& path)
{
    QFile::remove(path + ".3");
    QFile::rename(path + ".2", path + ".3");
    QFile::rename(path + ".1", path + ".2");
    QFile::rename(path, path + ".1");
}

void handler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    const char* level = "info";
    switch (type) {
    case QtDebugMsg: level = "debug"; break;
    case QtInfoMsg: level = "info"; break;
    case QtWarningMsg: level = "warn"; break;
    case QtCriticalMsg: level = "error"; break;
    case QtFatalMsg: level = "fatal"; break;
    }
    const QString line = QStringLiteral("%1 [%2] %3: %4\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), QLatin1String(level),
                                  QLatin1String(ctx.category ? ctx.category : "default"), msg);
    const QByteArray utf8 = line.toUtf8();
    std::lock_guard lk(g_mutex);
    std::fwrite(utf8.constData(), 1, size_t(utf8.size()), stderr);
    if (g_file && type != QtDebugMsg) {
        if (g_file->size() > kMaxBytes) {
            const QString path = g_file->fileName();
            g_file->close();
            rotate(path);
            g_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        }
        g_file->write(utf8);
        g_file->flush();
    }
    if (type == QtFatalMsg)
        std::abort();
}

} // namespace

QString logDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/logs";
}

void installLogging()
{
    QDir().mkpath(logDirectory());
    {
        std::lock_guard lk(g_mutex);
        g_file = new QFile(logDirectory() + "/bad-connection-studio.log");
        if (!g_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            delete g_file;
            g_file = nullptr;
        }
    }
    qInstallMessageHandler(handler);
    installFfmpegLogBridge([](int level, const std::string& line) {
        if (level <= AV_LOG_ERROR)
            qCWarning(lcFfmpeg).noquote() << QString::fromStdString(line);
        else
            qCInfo(lcFfmpeg).noquote() << QString::fromStdString(line);
    });
}

} // namespace bcs
