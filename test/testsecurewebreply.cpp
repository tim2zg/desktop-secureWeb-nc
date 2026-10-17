/*
 * SPDX-FileCopyrightText: 2026 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <QtTest>
#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include "libsync/securewebreply.h"

class TestSecureWebReply : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void davRoundTrip()
    {
        QNetworkRequest request(QUrl(QStringLiteral("https://cloud.example/remote.php/dav/a%20b")));
        request.setAttribute(QNetworkRequest::CustomVerbAttribute, "PROPFIND");
        request.setRawHeader("Depth", "1");
        QBuffer body;
        body.setData("xml");
        body.open(QIODevice::ReadOnly);
        OCC::SecureWebReply reply(QNetworkAccessManager::CustomOperation, request, &body,
            QStringLiteral("success"), nullptr, QCoreApplication::applicationFilePath());
        QSignalSpy done(&reply, &QNetworkReply::finished);
        QVERIFY(done.wait());
        QCOMPARE(reply.error(), QNetworkReply::NoError);
        QCOMPARE(reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 207);
        const auto captured = QJsonDocument::fromJson(reply.readAll()).object();
        QCOMPARE(captured.value(QStringLiteral("method")).toString(), QStringLiteral("PROPFIND"));
        QCOMPARE(captured.value(QStringLiteral("url")).toString(), request.url().toString(QUrl::FullyEncoded));
        QCOMPARE(captured.value(QStringLiteral("body")).toString(), QStringLiteral("eG1s"));
    }
    void malformedResponseFails()
    {
        QNetworkRequest request(QUrl(QStringLiteral("https://cloud.example/")));
        OCC::SecureWebReply reply(QNetworkAccessManager::GetOperation, request, nullptr,
            QStringLiteral("malformed"), nullptr, QCoreApplication::applicationFilePath());
        QSignalSpy done(&reply, &QNetworkReply::finished);
        QVERIFY(done.wait());
        QCOMPARE(reply.error(), QNetworkReply::ProtocolFailure);
    }
    void cancellationFinishesOnce()
    {
        QNetworkRequest request(QUrl(QStringLiteral("https://cloud.example/")));
        OCC::SecureWebReply reply(QNetworkAccessManager::GetOperation, request, nullptr,
            QStringLiteral("success"), nullptr, QCoreApplication::applicationFilePath());
        QSignalSpy done(&reply, &QNetworkReply::finished);
        reply.abort();
        QTest::qWait(20);
        QCOMPARE(done.count(), 1);
        QCOMPARE(reply.error(), QNetworkReply::OperationCanceledError);
    }
};

int main(int argc, char **argv)
{
    if (argc == 3 && QByteArray(argv[1]) == "--profile") {
        QByteArray input;
        for (int value = std::getchar(); value != EOF; value = std::getchar()) { input.append(static_cast<char>(value)); }
        const auto output = QByteArray(argv[2]) == "malformed" ? QByteArray("not json")
            : QJsonDocument(QJsonObject{{QStringLiteral("status"), 207}, {QStringLiteral("headers"), QJsonArray{}},
                {QStringLiteral("body"), QString::fromLatin1(input.toBase64())}}).toJson(QJsonDocument::Compact);
        std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout);
        return 0;
    }
    QCoreApplication application(argc, argv);
    TestSecureWebReply test;
    return QTest::qExec(&test, argc, argv);
}
#include "testsecurewebreply.moc"
