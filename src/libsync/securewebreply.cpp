/*
 * SPDX-FileCopyrightText: 2026 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "securewebreply.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>
#include <cstring>

namespace OCC {
namespace {
constexpr qint64 MaxBody = 32 * 1024 * 1024;
constexpr qint64 MaxOutput = 48 * 1024 * 1024;
constexpr int TimeoutMs = 80000;
}

QString SecureWebReply::profilePath()
{
    static const auto path = [] {
        const auto configured = qEnvironmentVariable("SECUREWEB_NEXTCLOUD_PROFILE");
        if (!configured.isEmpty()) {
            return configured;
        }
        const auto candidate = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/secureweb.json");
        return QFileInfo::exists(candidate) ? candidate : QString{};
    }();
    return path;
}

SecureWebReply::SecureWebReply(QNetworkAccessManager::Operation operation, const QNetworkRequest &request, QIODevice *data,
    const QString &profile, QObject *parent, const QString &program)
    : QNetworkReply(parent), _process(new QProcess(this))
{
    setRequest(request);
    setUrl(request.url());
    setOperation(operation);
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    QTimer::singleShot(0, this, [this, operation, request, data, profile, program] {
        if (isFinished()) { return; }
        QByteArray method;
        switch (operation) {
        case QNetworkAccessManager::GetOperation: method = "GET"; break;
        case QNetworkAccessManager::HeadOperation: method = "HEAD"; break;
        case QNetworkAccessManager::PutOperation: method = "PUT"; break;
        case QNetworkAccessManager::PostOperation: method = "POST"; break;
        case QNetworkAccessManager::DeleteOperation: method = "DELETE"; break;
        case QNetworkAccessManager::CustomOperation: method = request.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray(); break;
        default: fail(ProtocolInvalidOperationError, QStringLiteral("Unsupported SecureWeb method")); return;
        }
        QByteArray body;
        if (data) {
            if (data->isSequential()) {
                fail(ProtocolInvalidOperationError, QStringLiteral("SecureWeb requires a bounded upload device")); return;
            }
            body = data->read(MaxBody + 1);
            if (body.size() > MaxBody || !data->atEnd()) {
                fail(ContentOperationNotPermittedError, QStringLiteral("SecureWeb transfer exceeds 32 MiB; use upload chunks")); return;
            }
        }
        QJsonArray headers;
        for (const auto &name : request.rawHeaderList()) {
            headers.append(QJsonArray{QString::fromLatin1(name), QString::fromLatin1(request.rawHeader(name))});
        }
        const auto payload = QJsonDocument(QJsonObject{
            {QStringLiteral("method"), QString::fromLatin1(method)},
            {QStringLiteral("url"), request.url().toString(QUrl::FullyEncoded)},
            {QStringLiteral("headers"), headers},
            {QStringLiteral("body"), QString::fromLatin1(body.toBase64())}
        }).toJson(QJsonDocument::Compact);
        connect(_process, &QProcess::readyReadStandardOutput, this, [this] {
            _output += _process->readAllStandardOutput();
            if (_output.size() > MaxOutput) {
                fail(ProtocolFailure, QStringLiteral("SecureWeb response exceeds limit"));
            }
        });
        connect(_process, &QProcess::readyReadStandardError, this, [this] { _process->readAllStandardError(); });
        connect(_process, &QProcess::errorOccurred, this, [this] {
            fail(UnknownNetworkError, QStringLiteral("SecureWeb transport could not start"));
        });
        connect(_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            if (isFinished()) { return; }
            if (code != 0 || status != QProcess::NormalExit) {
                fail(UnknownNetworkError, QStringLiteral("SecureWeb encrypted request failed; check profile, session and service")); return;
            }
            complete();
        });
        auto executable = program;
        if (executable.isEmpty()) {
            executable = QCoreApplication::applicationDirPath() + QStringLiteral("/secureweb-desktop-transport");
#ifdef Q_OS_WIN
            executable += QStringLiteral(".exe");
#endif
        }
        _process->setProgram(executable);
        _process->setArguments({QStringLiteral("--profile"), profile});
        connect(_process, &QProcess::started, this, [this, payload] {
            _process->write(payload);
            _process->closeWriteChannel();
            Q_EMIT requestSent();
        });
        _process->start();
        QTimer::singleShot(TimeoutMs, this, [this] {
            if (!isFinished()) { fail(TimeoutError, QStringLiteral("SecureWeb request timed out")); }
        });
    });
}

void SecureWebReply::complete()
{
    _output += _process->readAllStandardOutput();
    QJsonParseError parseError;
    const auto result = QJsonDocument::fromJson(_output, &parseError);
    const auto object = result.object();
    const auto status = object.value(QStringLiteral("status")).toInt();
    if (parseError.error != QJsonParseError::NoError || status < 100 || status > 599
        || !object.value(QStringLiteral("body")).isString() || !object.value(QStringLiteral("headers")).isArray()) {
        fail(ProtocolFailure, QStringLiteral("Invalid SecureWeb response")); return;
    }
    const auto decoded = QByteArray::fromBase64Encoding(object.value(QStringLiteral("body")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.size() > MaxBody) {
        fail(ProtocolFailure, QStringLiteral("Invalid SecureWeb response body")); return;
    }
    _body = decoded.decoded;
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
    for (const auto &entry : object.value(QStringLiteral("headers")).toArray()) {
        const auto pair = entry.toArray();
        if (pair.size() != 2 || !pair[0].isString() || !pair[1].isString()) {
            fail(ProtocolFailure, QStringLiteral("Invalid SecureWeb response headers")); return;
        }
        const auto name = pair[0].toString().toLatin1();
        const auto value = pair[1].toString().toLatin1();
        if (name.contains('\r') || name.contains('\n') || value.contains('\r') || value.contains('\n')) {
            fail(ProtocolFailure, QStringLiteral("Invalid SecureWeb response header")); return;
        }
        setRawHeader(name, value);
        if (name.compare("location", Qt::CaseInsensitive) == 0) {
            setAttribute(QNetworkRequest::RedirectionTargetAttribute, QUrl(QString::fromLatin1(value)));
        }
    }
    if (status >= 400) {
        setError(status == 401 ? AuthenticationRequiredError : status == 403 ? ContentAccessDenied : status == 404 ? ContentNotFoundError : UnknownContentError,
            QStringLiteral("SecureWeb service returned HTTP %1").arg(status));
        Q_EMIT errorOccurred(error());
    }
    Q_EMIT metaDataChanged();
    Q_EMIT downloadProgress(_body.size(), _body.size());
    if (!_body.isEmpty()) { Q_EMIT readyRead(); }
    setFinished(true);
    Q_EMIT finished();
}

void SecureWebReply::fail(QNetworkReply::NetworkError error, const QString &message)
{
    if (isFinished()) { return; }
    setFinished(true);
    _process->kill();
    setError(error, message);
    Q_EMIT errorOccurred(error);
    Q_EMIT finished();
}

void SecureWebReply::abort() { fail(OperationCanceledError, QStringLiteral("SecureWeb request cancelled")); }
qint64 SecureWebReply::bytesAvailable() const { return _body.size() - _position + QNetworkReply::bytesAvailable(); }
qint64 SecureWebReply::readData(char *data, qint64 maxSize)
{
    const auto count = qMin(maxSize, _body.size() - _position);
    if (count <= 0) { return isFinished() ? -1 : 0; }
    std::memcpy(data, _body.constData() + _position, static_cast<size_t>(count));
    _position += count;
    return count;
}
}
