/*
 * SPDX-FileCopyrightText: 2026 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "owncloudlib.h"
#include <QNetworkReply>
#include <QProcess>

namespace OCC {
/** @brief Adapts the bundled SecureWeb transport to Qt network jobs. */
class OWNCLOUDSYNC_EXPORT SecureWebReply : public QNetworkReply
{
    Q_OBJECT
public:
    SecureWebReply(QNetworkAccessManager::Operation operation, const QNetworkRequest &request, QIODevice *data,
        const QString &profile, QObject *parent, const QString &program = {});
    /** @brief Returns the profile selected when the network layer first starts. */
    static QString profilePath();
    void abort() override;
    qint64 bytesAvailable() const override;
    bool isSequential() const override { return true; }
protected:
    qint64 readData(char *data, qint64 maxSize) override;
private:
    void complete();
    void fail(QNetworkReply::NetworkError error, const QString &message);
    QProcess *_process;
    QByteArray _output;
    QByteArray _body;
    qint64 _position = 0;
};
}
