/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 TOPST SBC Project
 */

#include "fwdnflashthread.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDebug>
#include <QThread>

FwdnFlashThread::FwdnFlashThread(const QString &boardType,
                                 const QString &port,
                                 const QUrl &src,
                                 quint64 downloadLen,
                                 quint64 extrLen,
                                 QObject *parent)
    : QThread(parent)
    , _boardType(boardType)
    , _port(port)
    , _src(src)
    , _downloadLen(downloadLen)
    , _extrLen(extrLen)
{
}

FwdnFlashThread::~FwdnFlashThread()
{
    cancel();
    wait(5000);
}

void FwdnFlashThread::cancel()
{
    QMutexLocker lock(&_mutex);
    _cancelled = true;
    if (_currentProcess && _currentProcess->state() != QProcess::NotRunning) {
        _currentProcess->kill();
    }
}

QString FwdnFlashThread::resolveToolPath(const QString &toolSubdir, const QString &binaryName)
{
    QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        appDir + "/tools/" + toolSubdir + "/" + binaryName,
        appDir + "/" + toolSubdir + "/" + binaryName,
        QDir::currentPath() + "/src/tools/" + toolSubdir + "/" + binaryName,
        QDir::currentPath() + "/tools/" + toolSubdir + "/" + binaryName
    };

    for (const QString &path : candidates) {
        if (QFile::exists(path)) {
            return QDir::cleanPath(path);
        }
    }

    return binaryName; // Fallback to PATH
}

bool FwdnFlashThread::runStep(const QString &program,
                             const QStringList &arguments,
                             const QString &workingDir,
                             const QString &stepDesc,
                             int progressBase,
                             int progressRange)
{
    {
        QMutexLocker lock(&_mutex);
        if (_cancelled) return false;
    }

    emit preparationStatusUpdate(stepDesc);
    qDebug() << "FwdnFlashThread: Running" << program << arguments << "in" << workingDir;

    QProcess process;
    {
        QMutexLocker lock(&_mutex);
        _currentProcess = &process;
    }

    process.setWorkingDirectory(workingDir);
    process.setProgram(program);
    process.setArguments(arguments);

    // Regex to match percentage if emitted by fwdn
    static const QRegularExpression pctRegex(QStringLiteral(R"((\d{1,3})\s*%)"));

    quint64 totalBytes = _extrLen > 0 ? _extrLen : 1000000;

    connect(&process, &QProcess::readyReadStandardOutput, this, [this, &process, progressBase, progressRange, totalBytes]() {
        QByteArray output = process.readAllStandardOutput();
        QString str = QString::fromLocal8Bit(output);
        qDebug().noquote() << "[FWDN stdout]" << str.trimmed();

        auto match = pctRegex.match(str);
        if (match.hasMatch()) {
            int pct = match.captured(1).toInt();
            pct = qBound(0, pct, 100);
            int overallPct = progressBase + (pct * progressRange / 100);
            quint64 nowBytes = (quint64)overallPct * totalBytes / 100;
            emit writeProgress(nowBytes, totalBytes);
        }
    });

    connect(&process, &QProcess::readyReadStandardError, this, [&process]() {
        QByteArray err = process.readAllStandardError();
        qWarning().noquote() << "[FWDN stderr]" << QString::fromLocal8Bit(err).trimmed();
    });

    process.start();
    if (!process.waitForStarted(10000)) {
        QMutexLocker lock(&_mutex);
        _currentProcess = nullptr;
        emit error(tr("Failed to start FWDN process: %1").arg(program));
        return false;
    }

    // Emit base progress for the step
    emit writeProgress((quint64)progressBase * totalBytes / 100, totalBytes);

    while (process.state() == QProcess::Running) {
        {
            QMutexLocker lock(&_mutex);
            if (_cancelled) {
                process.kill();
                _currentProcess = nullptr;
                return false;
            }
        }
        process.waitForFinished(200);
    }

    {
        QMutexLocker lock(&_mutex);
        _currentProcess = nullptr;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (!_cancelled) {
            emit error(tr("FWDN operation failed during '%1' with exit code %2")
                       .arg(stepDesc)
                       .arg(process.exitCode()));
        }
        return false;
    }

    emit writeProgress((quint64)(progressBase + progressRange) * totalBytes / 100, totalBytes);
    return true;
}

void FwdnFlashThread::run()
{
    QString localImagePath = _src.isLocalFile() ? _src.toLocalFile() : _src.toString();
    QFileInfo imgInfo(localImagePath);
    if (!imgInfo.exists()) {
        emit error(tr("Image file not found: %1").arg(localImagePath));
        return;
    }

    if (_extrLen == 0) {
        _extrLen = imgInfo.size();
    }

    bool isAi = (_boardType == QStringLiteral("topst-ai"));
    QString toolSubdir = isAi ? QStringLiteral("fwdn_ai") : QStringLiteral("fwdn_d3");

#if defined(Q_OS_WIN) || defined(_WIN32)
    QString binName = isAi ? QStringLiteral("fwdn_ai.exe") : QStringLiteral("fwdn.exe");
#else
    QString binName = isAi ? QStringLiteral("fwdn_ai") : QStringLiteral("fwdn");
#endif

    QString toolExec = resolveToolPath(toolSubdir, binName);
    QFileInfo toolInfo(toolExec);
    QString toolDir = toolInfo.absolutePath();

    qDebug() << "FwdnFlashThread: Target board:" << _boardType
             << "toolDir:" << toolDir
             << "toolExec:" << toolExec
             << "image:" << localImagePath;

    // Check if tool executable exists
    if (!toolInfo.exists()) {
        emit error(tr("FWDN tool binary not found at %1. Please verify TOPST tools installation.").arg(toolExec));
        return;
    }

    // Step 1: Bootstrap into FWDN mode
    QStringList step1Args;
    if (isAi) {
        step1Args << "--fwdn" << "boot-firmware/tcc750x_fwdn.json";
        if (!_port.isEmpty()) step1Args << "--port" << _port;
    } else {
        step1Args << "--fwdn" << "boot-firmware/fwdn.json";
    }

    if (!runStep(toolExec, step1Args, toolDir, tr("Step 1/4: Bootstrapping TOPST board into FWDN mode..."), 0, 15))
        return;

    // Small delay between USB re-enumeration
    QThread::msleep(1000);

    // Step 2: Low-format internal eMMC storage
    QStringList step2Args;
    if (isAi) {
        step2Args << "--fwdn" << "boot-firmware/tcc750x_fwdn.json"
                  << "--storage" << "emmc" << "--low-format";
        if (!_port.isEmpty()) step2Args << "--port" << _port;
    } else {
        step2Args << "--storage" << "emmc" << "--low-format";
    }

    if (!runStep(toolExec, step2Args, toolDir, tr("Step 2/4: Initializing and low-formatting eMMC..."), 15, 15))
        return;

    QThread::msleep(500);

    // Step 3: Write boot firmware
    QStringList step3Args;
    if (isAi) {
        step3Args << "--fwdn" << "boot-firmware/tcc750x_fwdn.json"
                  << "-w" << "boot-firmware/tcc750x_boot.json";
        if (!_port.isEmpty()) step3Args << "--port" << _port;
    } else {
        step3Args << "-w" << "boot-firmware/boot.single.json";
    }

    if (!runStep(toolExec, step3Args, toolDir, tr("Step 3/4: Writing bootloader firmware..."), 30, 15))
        return;

    QThread::msleep(500);

    // Step 4: Write main system FAI image to eMMC user area
    emit writing();

    QStringList step4Args;
    if (isAi) {
        step4Args << "--fwdn" << "boot-firmware/tcc750x_fwdn.json"
                  << "-w" << localImagePath
                  << "--storage" << "emmc"
                  << "--area" << "user";
        if (!_port.isEmpty()) step4Args << "--port" << _port;
    } else {
        step4Args << "-w" << localImagePath
                  << "--storage" << "emmc"
                  << "--area" << "user";
    }

    if (!runStep(toolExec, step4Args, toolDir, tr("Step 4/4: Flashing OS image (.fai) to eMMC..."), 45, 55))
        return;

    emit finalizing();
    QThread::msleep(500);
    emit success();
}

