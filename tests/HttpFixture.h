#pragma once

#include <QJsonDocument>
#include <QMap>
#include <QTcpServer>
#include <QTcpSocket>
#include <functional>

struct FixtureRequest {
    QByteArray method;
    QUrl url;
    QMap<QByteArray, QByteArray> headers;
    QByteArray body;
    QJsonObject json() const { return QJsonDocument::fromJson(body).object(); }
};

struct FixtureResponse {
    int status = 200;
    QByteArray body = "{}";
    QMap<QByteArray, QByteArray> headers;
    bool dropConnection = false;
};

class HttpFixture : public QTcpServer {
public:
    HttpFixture() {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                    buffer->append(socket->readAll());
                    const auto end = buffer->indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    const auto lines = buffer->left(end).split('\n');
                    FixtureRequest request;
                    const auto first = lines.first().trimmed().split(' ');
                    request.method = first.value(0);
                    request.url = QUrl::fromEncoded(first.value(1));
                    for (const auto &line : lines.mid(1)) {
                        const auto colon = line.indexOf(':');
                        if (colon > 0)
                            request.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
                    }
                    const auto length = request.headers.value("content-length").toLongLong();
                    if (buffer->size() < end + 4 + length)
                        return;
                    request.body = buffer->mid(end + 4, length);
                    disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
                    requests.append(request);
                    const auto response = handler ? handler(request) : FixtureResponse{};
                    if (response.dropConnection) {
                        socket->abort();
                        socket->deleteLater();
                        return;
                    }
                    QByteArray bytes = "HTTP/1.1 " + QByteArray::number(response.status) + " Fixture\r\n";
                    bytes += "Content-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(response.body.size()) + "\r\n";
                    for (auto it = response.headers.begin(); it != response.headers.end(); ++it)
                        bytes += it.key() + ": " + it.value() + "\r\n";
                    socket->write(bytes + "\r\n" + response.body);
                    socket->disconnectFromHost();
                    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                });
            }
        });
    }
    QUrl base(const QString &path = {}) const { return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path)); }
    QList<FixtureRequest> requests;
    std::function<FixtureResponse(const FixtureRequest &)> handler;
};
