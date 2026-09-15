#ifndef FWDNFLASHTHREAD_H
#define FWDNFLASHTHREAD_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 TOPST SBC Project
 */

#include <QThread>
#include <QString>
#include <QUrl>
#include <QProcess>
#include <QMutex>

class FwdnFlashThread : public QThread
{
    Q_OBJECT

public:
    explicit FwdnFlashThread(const QString &boardType,
                             const QString &port,
                             const QUrl &src,
                             quint64 downloadLen = 0,
                             quint64 extrLen = 0,
                             QObject *parent = nullptr);
    ~FwdnFlashThread() override;

    void cancel();

signals:
    void preparationStatusUpdate(const QString &msg);
    void writing();
    void finalizing();
    void writeProgress(quint64 now, quint64 total);
    void downloadProgress(quint64 now, quint64 total);
    void error(const QString &msg);
    void success();

protected:
    void run() override;

private:
    bool runStep(const QString &program, const QStringList &arguments, const QString &workingDir, const QString &stepDesc, int progressBase = 0, int progressRange = 0);
    QString resolveToolPath(const QString &toolDir, const QString &binaryName);

    QString _boardType;
    QString _port;
    QUrl _src;
    quint64 _downloadLen;
    quint64 _extrLen;

    QMutex _mutex;
    bool _cancelled = false;
    QProcess *_currentProcess = nullptr;
};

#endif // FWDNFLASHTHREAD_H

