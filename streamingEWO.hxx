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
    // Enum for box position
    enum BoxPosition {
        TopLeft = 1,
        TopRight = 2,
        BottomRight = 3,
        BottomLeft = 4
    };
    Q_ENUM(BoxPosition)
    // Enum for transport protocol
    enum TransportProtocol {
        WebSocket,
        UDP
    };
    Q_ENUM(TransportProtocol)

  // Q_PROPERTY declarations for all invokeMethod functions
  Q_PROPERTY(QString webSocketUrl READ getWebSocketUrl WRITE setWebSocketUrl DESIGNABLE true SCRIPTABLE true)
  Q_PROPERTY(QString rtspStreamUrl READ getRtspStreamUrl WRITE setRtspStreamUrl DESIGNABLE true SCRIPTABLE true)
  Q_PROPERTY(bool debugMode READ getDebugMode WRITE setDebugMode DESIGNABLE true SCRIPTABLE true)
  Q_PROPERTY(bool debugPrint READ getDebugPrint WRITE setDebugPrint DESIGNABLE true SCRIPTABLE true)
  Q_PROPERTY(TransportProtocol transport READ getTransport WRITE setTransport DESIGNABLE true SCRIPTABLE true)
  Q_PROPERTY(int udpPort READ getUdpPort WRITE setUdpPort DESIGNABLE true SCRIPTABLE true)
  Q_PROPERTY(QString streamName READ getStreamName WRITE setStreamName DESIGNABLE true SCRIPTABLE true)
  Q_PROPERTY(BoxPosition streamNameBoxPosition READ getStreamNameBoxPosition WRITE setStreamNameBoxPosition DESIGNABLE true SCRIPTABLE true)
  Q_PROPERTY(bool inGedi READ isInGedi WRITE setInGedi DESIGNABLE false SCRIPTABLE false)


  public:
    MyWidget(QWidget *parent);
    ~MyWidget();
    // Setters and getters for all Q_PROPERTYs
    void setWebSocketUrl(const QString &url);
    QString getWebSocketUrl() const;
    void setRtspStreamUrl(const QString &url);
    QString getRtspStreamUrl() const;
    void setDebugMode(bool enabled);
    bool getDebugMode() const;
    void setDebugPrint(bool enabled);
    bool getDebugPrint() const;
    void setTransport(TransportProtocol protocol);
    TransportProtocol getTransport() const;
    void setUdpPort(int port);
    int getUdpPort() const;
    void setStreamName(const QString &name, int position = -1);
    QString getStreamName() const;
    BoxPosition getStreamNameBoxPosition() const;
    void setStreamNameBoxPosition(BoxPosition pos);
    bool isInGedi() const;
    void setInGedi(bool inGedi);

  protected:
    virtual void paintEvent(QPaintEvent *event);
    virtual void mousePressEvent(QMouseEvent *event);

  private slots:
    void onConnected();
    void onBinaryMessageReceived(const QByteArray &message);
    void onDisconnected();
    void checkConnectionStatus();
    void onUdpDatagramReceived();
    void onTextMessageReceived(const QString &message);

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
    TransportProtocol m_transport = WebSocket;
    int m_udpPort = 4635;
    QUdpSocket* m_udpSocket = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    // Stream name overlay members
    QString m_streamName;
    BoxPosition m_streamNameBoxPosition = TopLeft;
    bool m_inGedi = false;
    // Undistortion members
    bool m_undistortionAvailable = false;
    bool m_undistortionEnabled = false;
    int m_undistortionMode = 0; // 0=off, 1=alpha=1.0, 2=alpha=0.0
    QRect m_undistortButtonRect;
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
