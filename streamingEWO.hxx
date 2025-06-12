#ifndef _streamingEWO_H_
#define _streamingEWO_H_

#include <BaseExternWidget.hxx>
#include <QWebSocket>
#include <QImage>
#include <QTimer>
#include <QUdpSocket>

//--------------------------------------------------------------------------------
// this is the real widget (an ordinary Qt widget), which can also use Q_PROPERTY

class MyWidget : public QWidget
{
  Q_OBJECT

  public:
    MyWidget(QWidget *parent);

    void setWebSocketUrl(const QString &url);
    void setRtspStreamUrl(const QString &url);
    void setDebugMode(bool enabled);
    bool getDebugMode() const;
    void setDebugPrint(bool enabled);
    bool getDebugPrint() const;
    void setTransport(const QString& transport);
    void setUdpHost(const QString& host);
    void setUdpPort(int port);

  protected:
    virtual void paintEvent(QPaintEvent *event);

  private slots:
    void onConnected();
    void onBinaryMessageReceived(const QByteArray &message);
    void onDisconnected();
    void checkConnectionStatus();
    void onUdpDatagramReceived();

  private:
    void setupUdpSocket();
    void closeUdpSocket();

    QWebSocket *m_webSocket;
    QImage m_image;
    QString m_statusText;
    QTimer m_connectionStatusTimer;
    QString m_webSocketUrl;
    QString m_rtspStreamUrl;
    qint64 m_lastFrameTimestamp;
    bool m_debugMode; // New member for debug state
    bool m_debugPrint;
    bool m_overLatencyCutoff = false; // New member to track latency cutoff
    qint64 m_currentDelayMs; // New member to store current delay
    QString m_transport = "websocket";
    int m_udpPort = 4635;

    // Add member for UDP socket
    QUdpSocket* m_udpSocket = nullptr;
};

//--------------------------------------------------------------------------------
// this is the EWO interface class

class EWO_EXPORT streamingEWO : public BaseExternWidget
{
  Q_OBJECT

  public:
    streamingEWO(QWidget *parent);

    virtual QWidget *widget() const;

    virtual QStringList signalList() const;

    virtual bool methodInterface(const QString &name, QVariant::Type &retVal,
                                 QList<QVariant::Type> &args) const;

    virtual QStringList methodList() const;

  public slots:
    virtual QVariant invokeMethod(const QString &name, QList<QVariant> &values, QString &error);

  private:
    MyWidget *baseWidget;
};

#endif
