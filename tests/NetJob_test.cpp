#include <QElapsedTimer>
#include <QHash>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <net/NetJob.h>

/* A tiny HTTP server: each path answers with a scripted list of statuses, the last one repeating. */
class ScriptedServer : public QObject {
    Q_OBJECT
   public:
    QHash<QString, QList<int>> script;
    QHash<QString, int> hits;
    QByteArray retryAfter;

    ScriptedServer()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = m_server.nextPendingConnection())
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { answer(socket); });
        });
        m_server.listen(QHostAddress::LocalHost);
    }
    QUrl url(const QString& path) const { return QUrl(QString("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path)); }

   private:
    void answer(QTcpSocket* socket)
    {
        const auto request = socket->readAll();
        if (!request.contains("\r\n\r\n"))
            return;
        const auto path = QString::fromLatin1(request.split(' ').value(1));
        const int hit = hits[path]++;
        const auto& statuses = script[path];
        const int status = statuses.value(qMin(hit, static_cast<int>(statuses.size()) - 1), 404);
        const QByteArray body = status == 200 ? "ok" : "no";
        QByteArray reply = "HTTP/1.1 " + QByteArray::number(status) + " Scripted\r\nContent-Length: " + QByteArray::number(body.size()) +
                           "\r\nConnection: close\r\n";
        if (status == 429 && !retryAfter.isEmpty())
            reply += "Retry-After: " + retryAfter + "\r\n";
        socket->write(reply + "\r\n" + body);
        socket->disconnectFromHost();
    }
    QTcpServer m_server;
};

class NetJobTest : public QObject {
    Q_OBJECT

    struct Result {
        bool succeeded;
        QByteArray body;
        qint64 elapsedMs;
    };

    // Outlives every request: requests are deleted later and their replies belong to the manager.
    QNetworkAccessManager m_network;

    Result fetch(ScriptedServer& server, const QString& path)
    {
        NetJob job("NetJob test", &m_network, 1);
        auto [request, output] = Net::Request::makeByteArray(server.url(path));
        job.addNetAction(request);
        QElapsedTimer timer;
        timer.start();
        job.start();
        if (!QTest::qWaitFor([&job] { return job.isFinished(); }, 30000))
            qFatal("NetJob did not finish");
        return { job.wasSuccessful(), *output, timer.elapsed() };
    }

   private slots:
    void test_retriesServiceUnavailable()
    {
        ScriptedServer server;
        server.script["/busy"] = { 503, 200 };
        const auto result = fetch(server, "/busy");
        QVERIFY(result.succeeded);
        QCOMPARE(result.body, QByteArray("ok"));
        QCOMPARE(server.hits["/busy"], 2);
    }

    void test_honoursRetryAfter()
    {
        ScriptedServer server;
        server.script["/limited"] = { 429, 200 };
        server.retryAfter = "3";
        const auto result = fetch(server, "/limited");
        QVERIFY(result.succeeded);
        QCOMPARE(server.hits["/limited"], 2);
        QVERIFY2(result.elapsedMs >= 2900, qPrintable(QString("retried after %1 ms").arg(result.elapsedMs)));
    }

    void test_doesNotRetryNotFound()
    {
        ScriptedServer server;
        server.script["/missing"] = { 404, 200 };
        QVERIFY(!fetch(server, "/missing").succeeded);
        QCOMPARE(server.hits["/missing"], 1);
    }

    void test_doesNotRetryServerError()
    {
        ScriptedServer server;
        server.script["/broken"] = { 500, 200 };
        QVERIFY(!fetch(server, "/broken").succeeded);
        QCOMPARE(server.hits["/broken"], 1);
    }
};

QTEST_GUILESS_MAIN(NetJobTest)

#include "NetJob_test.moc"
