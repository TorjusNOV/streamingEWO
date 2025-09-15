#include <streamingEWO.hxx>

// TODO change to what you need
#include <QPainter>
#include <QBuffer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime> // Required for QDateTime
#include <QDebug> // For debug prints
#include <QMouseEvent> // Required for mouse events
#include <QSvgRenderer>
#include <QNetworkInterface> // Required for getting local IP address

//--------------------------------------------------------------------------------

EWO_PLUGIN( streamingEWO )

namespace {
struct StatusMessages {
    const QString noConnection = "No connection to stream";
    const QString connecting = "Connecting to stream...";
    const QString considerableLatency = "Considerable latency in stream";
    const QString errorDecoding = "Error decoding image";
    const QString invalidFormat = "Invalid message format";
    const QString frozen = "Stream appears to be frozen";
};
static const StatusMessages statusMsg;
}

//--------------------------------------------------------------------------------
// Here comes the implementation of the widget itself.
// The widget itself is a normal Qt widget and does not need to know
// anything about WinCC OA or the EWO interface
//--------------------------------------------------------------------------------

/**
 * \brief MyWidget::MyWidget
 * Constructor for MyWidget. Initializes all member variables and sets up connections and timers.
 * \param parent The parent widget. Typically passed as 'this' from the parent container; may be nullptr for a top-level widget.
 */
MyWidget::MyWidget(QWidget *parent)
  : QWidget(parent),
    m_webSocket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this)),
    m_statusText("No connection to stream"),
    m_lastFrameTimestamp(0),
    m_debugMode(false), // Initialize debug mode to false
    m_debugPrint(false), // Initialize debug print to false
    m_currentDelayMs(0), // Initialize current delay
    m_streamName(),
    m_streamNameBoxPosition(TopLeft)
{
  // Note: m_transport and m_udpPort are initialized in the header with default values
  if (m_debugPrint) qDebug() << "[DEBUG] MyWidget constructor called. Initial UDP port:" << m_udpPort << "Transport:" << (m_transport == UDP ? "UDP" : "WebSocket");
  connect(m_webSocket, &QWebSocket::connected, this, &MyWidget::onConnected);
  connect(m_webSocket, &QWebSocket::disconnected, this, &MyWidget::onDisconnected);
  connect(m_webSocket, &QWebSocket::binaryMessageReceived, this, &MyWidget::onBinaryMessageReceived);
  connect(m_webSocket, &QWebSocket::textMessageReceived, this, &MyWidget::onTextMessageReceived);

  connect(&m_connectionStatusTimer, &QTimer::timeout, this, &MyWidget::checkConnectionStatus);
  m_connectionStatusTimer.start(500); // Check connection status every 0.5 seconds

  // Set initial background to green
  QPalette pal = palette();
  pal.setColor(backgroundRole(), Qt::green);
  setAutoFillBackground(true);
  setPalette(pal);
  
  // Enable touch events for touch interface support
  setAttribute(Qt::WA_AcceptTouchEvents, true);
  // Also enable synthetic mouse events from touch as fallback
  setAttribute(Qt::WA_TouchPadAcceptSingleTouchEvents, true);
}

MyWidget::~MyWidget()
{
    // Safely close WebSocket connection
    if (m_webSocket) {
        m_webSocket->close();
    }
    // Safely close UDP socket
    closeUdpSocket();
    // Stop connection status timer
    m_connectionStatusTimer.stop();
    // Stop reconnect timer if exists
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
    }
}
//--------------------------------------------------------------------------------

/**
 * \brief MyWidget::setWebSocketUrl
 * Sets the WebSocket URL and opens a connection if needed.
 * \param url The WebSocket server URL as a QString (e.g., ws://host:port/path).
 */
void MyWidget::setWebSocketUrl(const QString &url)
{
    if (m_webSocketUrl == url)
        return; // Avoid unnecessary update
    if (m_debugPrint) qDebug() << "[DEBUG] setWebSocketUrl called with" << url;
    m_webSocketUrl = url;
    if (m_inGedi) return; // Do not connect in editor
    if (!m_webSocketUrl.isEmpty() && m_webSocket->state() == QAbstractSocket::UnconnectedState)
    {        
        m_webSocket->open(QUrl(m_webSocketUrl));
        if (m_debugPrint) qDebug() << "[DEBUG] setWebSocketUrl opened connection to" << m_webSocketUrl;
    }
}

/**
 * \brief MyWidget::setRtspStreamUrl
 * Sets the RTSP stream URL and sends a control message if connected.
 * \param url The RTSP stream URL as a QString (e.g., rtsp://host:port/path).
 */
void MyWidget::setRtspStreamUrl(const QString &url)
{
    if (m_rtspStreamUrl == url)
        return; // Avoid unnecessary update
    if (m_debugPrint) qDebug() << "[DEBUG] setRtspStreamUrl called with" << url;
    m_rtspStreamUrl = url;
    if (m_webSocket->state() == QAbstractSocket::ConnectedState && !m_rtspStreamUrl.isEmpty())
    {
        QJsonObject message;
        message["type"] = "control";
        message["command"] = "set_stream";
        message["url"] = m_rtspStreamUrl;
        message["transport"] = (m_transport == UDP ? "udp" : "websocket");
        if (m_frameDropRatio > 1) {
            message["frame_drop_ratio"] = m_frameDropRatio;
        }
        if (m_transport == UDP) {
            message["udp_port"] = m_udpPort;
        }
        m_webSocket->sendTextMessage(QJsonDocument(message).toJson(QJsonDocument::Compact));
    }
}

/**
 * \brief MyWidget::setDebugMode
 * Enables or disables debug mode and triggers a repaint if changed.
 * \param enabled Boolean flag: true to enable debug mode, false to disable.
 */
void MyWidget::setDebugMode(bool enabled)
{
    if (m_debugMode == enabled)
        return; // Avoid unnecessary update
    if (m_debugPrint) qDebug() << "[DEBUG] setDebugMode called with" << enabled;
    m_debugMode = enabled;
    update(); // Trigger a repaint to show/hide debug info
}

void MyWidget::setDebugPrint(bool enabled)
{
    if (m_debugPrint == enabled)
        return;
    m_debugPrint = enabled;
    if (m_debugPrint) qDebug() << "[DEBUG] setDebugPrint called with" << enabled;
}

bool MyWidget::getDebugMode() const
{
    return m_debugMode;
}

bool MyWidget::getDebugPrint() const
{
    return m_debugPrint;
}

/**
 * \brief MyWidget::onConnected
 * Slot called when the WebSocket is connected. Sends the RTSP stream URL if set.
 */
void MyWidget::onConnected()
{
    if (m_debugPrint) qDebug() << "[DEBUG] onConnected called. RTSP URL:" << m_rtspStreamUrl << "Transport:" << (m_transport == UDP ? "UDP" : "WebSocket") << "UDP Port:" << m_udpPort;
    m_statusText = statusMsg.connecting;
    m_undistortionAvailable = false; // Reset on new connection
    m_undistortionEnabled = false;
    m_undistortionMode = 0;
    if (!m_rtspStreamUrl.isEmpty())
    {
        QJsonObject message;
        message["type"] = "control";
        message["command"] = "set_stream";
        message["url"] = m_rtspStreamUrl;
        message["transport"] = (m_transport == UDP ? "udp" : "websocket");
        if (m_frameDropRatio > 1) {
            message["frame_drop_ratio"] = m_frameDropRatio;
        }
        if (m_transport == UDP) {
            message["udp_port"] = m_udpPort;
            
            // Get the client's local IP address
            QString localIp = getLocalIpAddress();
            if (!localIp.isEmpty()) {
                message["udp_ip"] = localIp;
                if (m_debugPrint) qDebug() << "[DEBUG] Using local IP for UDP:" << localIp;
            } else {
                if (m_debugPrint) qDebug() << "[DEBUG] Warning: Could not determine local IP address";
            }
            
            setupUdpSocket();
        }
        if (m_debugPrint) qDebug() << "[DEBUG] Sending control message:" << QJsonDocument(message).toJson(QJsonDocument::Compact);
        m_webSocket->sendTextMessage(QJsonDocument(message).toJson(QJsonDocument::Compact));
    }
    update();
}

/**
 * \brief MyWidget::onDisconnected
 * Slot called when the WebSocket is disconnected. Updates status and attempts reconnect if needed.
 */
void MyWidget::onDisconnected()
{
    if (m_debugPrint) qDebug() << "[DEBUG] onDisconnected called.";
    m_undistortionAvailable = false;
    m_undistortionEnabled = false;
    m_undistortionMode = 0;
    if (m_statusText != statusMsg.noConnection || !m_image.isNull()) {
        m_statusText = statusMsg.noConnection;
        m_image = QImage(); // Clear image
        update();
    }
    // Attempt to reconnect if URL is set
    if (m_inGedi) return; // Do not reconnect in editor
    if (!m_webSocketUrl.isEmpty()) {
        // Use a member QTimer for safe delayed reconnect
        if (!m_reconnectTimer) {
            m_reconnectTimer = new QTimer(this);
            m_reconnectTimer->setSingleShot(true);
            connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
                if (m_webSocket && m_webSocket->state() == QAbstractSocket::UnconnectedState) {
                    m_webSocket->open(QUrl(m_webSocketUrl));
                }
            });
        }
        m_reconnectTimer->start(5000);
    }
}

/**
 * \brief MyWidget::onBinaryMessageReceived
 * Slot called when a binary message is received. Decodes the image and updates status/delay.
 * \param message The received binary message as a QByteArray. The first 8 bytes are expected to be a qint64 timestamp, followed by JPEG image data.
 */
void MyWidget::onBinaryMessageReceived(const QByteArray &message)
{
    if (m_debugPrint) qDebug() << "[DEBUG] onBinaryMessageReceived called. Message size:" << message.size();
    qint64 prevDelay = m_currentDelayMs;
    QImage prevImage = m_image;
    QString prevStatus = m_statusText;

    // Assuming the first 8 bytes are the timestamp (qint64)
    if (message.size() > 8) {
        QByteArray timestampData = message.left(8);
        QByteArray imageData = message.mid(8);
        
        if (m_debugPrint) {
            qDebug() << "[DEBUG] Timestamp bytes:" << timestampData.toHex();
            qDebug() << "[DEBUG] Image data size:" << imageData.size() << "First few bytes:" << imageData.left(16).toHex();
        }

        
        QDataStream tsStream(timestampData);
        tsStream >> m_lastServerTimestamp;

        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        m_currentDelayMs = currentTime - m_lastServerTimestamp; // Store current delay
        if (m_debugPrint) qDebug() << "[DEBUG] onBinaryMessageReceived called. Current time, received time and delay:" << currentTime <<", " << m_lastServerTimestamp << ", " << m_currentDelayMs;

        bool overCutoff = m_currentDelayMs > 150;
        if (!m_debugMode && overCutoff) {
            if (m_statusText != statusMsg.considerableLatency) {
                m_statusText = statusMsg.considerableLatency;
                m_image = QImage(); // Clear image
            }
        } else {
            if (m_image.loadFromData(imageData, "JPEG")) {
                if (m_debugPrint) qDebug() << "[DEBUG] Image loaded successfully from JPEG data";
                if (!m_statusText.isEmpty())
                    m_statusText = QString(); // Clear status text if image is successfully loaded
                m_lastFrameTimestamp = currentTime; // Store timestamp of the valid frame
            } else {
                if (m_debugPrint) qDebug() << "[DEBUG] Failed to load image from JPEG data";
                if (m_statusText != statusMsg.errorDecoding) {
                    m_statusText = statusMsg.errorDecoding;
                    m_image = QImage(); // Clear image on error
                }
            }
        }
        // Store overCutoff for debug overlay
        m_overLatencyCutoff = (m_debugMode && overCutoff);
    } else {
        if (m_statusText != statusMsg.invalidFormat) {
            m_statusText = statusMsg.invalidFormat;
            m_image = QImage(); // Clear image
        }
        m_currentDelayMs = -1; // Indicate invalid delay
        m_overLatencyCutoff = false;
    }
    // Only update if something changed
    if (prevDelay != m_currentDelayMs || prevImage != m_image || prevStatus != m_statusText) {
        if (m_debugPrint) qDebug() << "[DEBUG] Frame update: delay=" << m_currentDelayMs << ", status=" << m_statusText;
        update(); // Trigger a repaint
        repaint(); // Force immediate repaint - try this if update() isn't working
    }
}

/**
 * \brief MyWidget::checkConnectionStatus
 * Checks the connection status and updates the widget if the connection is lost or frozen.
 */
void MyWidget::checkConnectionStatus()
{
    if (m_debugPrint) qDebug() << "[DEBUG] checkConnectionStatus called. WebSocket state:" << m_webSocket->state();
    if (m_inGedi) return; // Do not connect in editor
    bool needUpdate = false;
    if (m_webSocket->state() != QAbstractSocket::ConnectedState) {
        if (m_statusText != statusMsg.noConnection || !m_image.isNull()) {
            m_statusText = statusMsg.noConnection;
            m_image = QImage();
            needUpdate = true;
        }
        if (!m_webSocketUrl.isEmpty()) {
            m_webSocket->open(QUrl(m_webSocketUrl)); // Attempt to reconnect
        }
    } else {
        // Check if we are receiving frames
        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        if (m_lastFrameTimestamp > 0 && (currentTime - m_lastFrameTimestamp > 500)) { // No frame in 5s
            if (m_statusText != statusMsg.frozen || !m_image.isNull()) {
                m_statusText = statusMsg.frozen;
                m_image = QImage(); // Clear image
                needUpdate = true;
            }
        }
    }
    if (needUpdate) {
        if (m_debugPrint) qDebug() << "[DEBUG] Connection status changed. Status:" << m_statusText;
        update();
    }
}


/**
 * \brief MyWidget::paintEvent
 * Handles all custom painting for the widget, including the image, status text, and debug overlay.
 * \param event The paint event (QPaintEvent*), typically not used directly in this implementation.
 */
void MyWidget::paintEvent(QPaintEvent *)
{
  if (m_debugPrint) qDebug() << "[DEBUG] paintEvent called. Image null?" << m_image.isNull() << "Status text:" << m_statusText;
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  if (!m_image.isNull()) {
        if (m_debugPrint) qDebug() << "[DEBUG] Drawing image, size:" << m_image.size();
        // Calculate scaled size while preserving aspect ratio
        QSize widgetSize = rect().size();
        QSize imageSize = m_image.size();
        QSize scaledSize = imageSize.scaled(widgetSize, Qt::KeepAspectRatio);
        // Center the image in the widget
        int x = (widgetSize.width() - scaledSize.width()) / 2;
        int y = (widgetSize.height() - scaledSize.height()) / 2;
        QRect targetRect(x, y, scaledSize.width(), scaledSize.height());
        // Fill background with black
        painter.fillRect(rect(), Qt::black);
        // Draw the scaled image
        painter.drawImage(targetRect, m_image.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        
  } else {
        if (m_debugPrint) qDebug() << "[DEBUG] Drawing green background with status:" << m_statusText;
        // Explicitly draw green background
        painter.fillRect(rect(), Qt::darkGreen);

        // Draw status text
        if (!m_statusText.isEmpty()) {
            painter.setPen(Qt::black); // Will be overridden for text, but good for default
            painter.setFont(QFont("Roboto", 12, QFont::Bold));
            QRect textRect = rect().adjusted(10, 10, -10, -10); // Area for text

            // Calculate bounding rect for the text itself to size the background
            QRect actualTextBoundingRect = painter.boundingRect(textRect, Qt::AlignCenter | Qt::TextWordWrap, m_statusText);

            // Create a slightly larger rect for the background, centered with the text
            QRectF backgroundRect = actualTextBoundingRect;
            backgroundRect.adjust(-10, -5, 10, 5); // Add padding
            // Center the background rect within the widget, similar to how text is centered
            backgroundRect.moveCenter(rect().center());


            // Background for text
            QBrush textBgBrush(QColor(0, 0, 0, 128)); // Grey, translucent
            painter.setBrush(textBgBrush);
            painter.setPen(Qt::NoPen); // No border for the background
            painter.drawRoundedRect(backgroundRect, 10, 10); // Rounded corners

            painter.setPen(Qt::white); // Text color
            painter.drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, m_statusText);
        }
  }

  // Draw debug information if enabled
  if (m_debugMode) {
      // Show delay, server timestamp, current time, and RTSP URL
      QString serverTimestampStr = (m_lastServerTimestamp > 0)
          ? QDateTime::fromMSecsSinceEpoch(m_lastServerTimestamp).toString("yyyy-MM-dd HH:mm:ss.zzz")
          : "N/A";
      QString currentTimeStr = (m_inGedi ? "N/A" : QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"));
      // Extract server IP from m_webSocketUrl
      QString serverIp = "N/A";
      QUrl wsUrl(m_webSocketUrl);
      if (wsUrl.isValid() && !wsUrl.host().isEmpty()) {
          serverIp = wsUrl.host();
      }
      QString debugText = QString("Delay: %1 ms\nServer TS: %2\nClient TS: %3\nServer IP: %4\nRTSP: %5")
                              .arg(m_currentDelayMs >= 0 ? QString::number(m_currentDelayMs) : "N/A")
                              .arg(serverTimestampStr)
                              .arg(currentTimeStr)
                              .arg(serverIp)
                              .arg(m_rtspStreamUrl.isEmpty() ? "N/A" : m_rtspStreamUrl);
      if (m_overLatencyCutoff) {
          debugText.prepend("[!] Latency above cutoff!\n");
      }

      painter.setFont(QFont("Roboto", 10)); // Slightly smaller font for debug

      QFontMetrics fm = painter.fontMetrics();
      int numLines = debugText.count('\n') + 1;
      int textHeightForLines = fm.height() * numLines;
      int linePadding = 5; // Padding inside the text box, around the text
      int boxPadding = 5;  // Padding outside the text box, from widget edges

      // Calculate available width for the text content itself (inside the linePadding)
      int availableTextWidth = width() - 2 * boxPadding - 2 * linePadding;
      if (availableTextWidth < 0) availableTextWidth = 0;

      // Use a large integer height for calculation; boundingRect will determine actual wrapped height.
      int calcRectHeight = 10000; 

      // Calculate bounding rect for the text, wrapped within availableTextWidth
      QRect calculatedTextRect = fm.boundingRect(0, 0, availableTextWidth, calcRectHeight,
                                                 Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                                                 debugText);

      // Determine the actual width and height for the background box
      int boxContentWidth = calculatedTextRect.width();
      int boxContentHeight = calculatedTextRect.height();

      int actualBoxWidth = boxContentWidth + 2 * linePadding;
      int actualBoxHeight = boxContentHeight + 2 * linePadding;
      
      // Ensure minimum height for the box, e.g., for all lines
      actualBoxHeight = qMax(actualBoxHeight, textHeightForLines + 2 * linePadding);

      // Background rectangle for debug text, positioned with boxPadding from bottom-left
      QRectF debugTextBgRect(boxPadding, 
                             height() - actualBoxHeight - boxPadding, 
                             actualBoxWidth, 
                             actualBoxHeight);

      QBrush debugTextBgBrush(QColor(0, 0, 0, 128)); // Grey, translucent
      painter.setBrush(debugTextBgBrush);
      painter.setPen(Qt::NoPen); // No border for the background
      painter.drawRoundedRect(debugTextBgRect, 5, 5); // Rounded corners

      // Rectangle for drawing the text, inset from the background box by linePadding
      QRectF debugTextDrawRect = debugTextBgRect.adjusted(linePadding, linePadding, -linePadding, -linePadding);

      painter.setPen(Qt::white); // Text color for debug
      // Draw the text within the debugTextDrawRect, it will use this rect's width for wrapping.
      painter.drawText(debugTextDrawRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, debugText);
  } else if (!m_streamName.isEmpty()) {
      // Draw stream name box in selected corner
      painter.setFont(QFont("Roboto", 10));
      QFontMetrics fm = painter.fontMetrics();
      int linePadding = 5;
      int boxPadding = 5;
      QString nameText = m_streamName;
      QRect nameTextRect = fm.boundingRect(nameText);
      int actualBoxWidth = nameTextRect.width() + 2 * linePadding;
      int actualBoxHeight = nameTextRect.height() + 2 * linePadding;
      int x = 0, y = 0;
      switch (m_streamNameBoxPosition) {
        case TopLeft:
            x = boxPadding;
            y = boxPadding;
            break;
        case TopRight:
            x = width() - actualBoxWidth - boxPadding;
            y = boxPadding;
            break;
        case BottomRight:
            x = width() - actualBoxWidth - boxPadding;
            y = height() - actualBoxHeight - boxPadding;
            break;
        case BottomLeft:
            x = boxPadding;
            y = height() - actualBoxHeight - boxPadding;
            break;
        default:
            x = boxPadding;
            y = boxPadding;
      }
      QRectF nameBoxRect(x, y, actualBoxWidth, actualBoxHeight);
      QBrush nameBoxBrush(QColor(0, 0, 0, 128));
      painter.setBrush(nameBoxBrush);
      painter.setPen(Qt::NoPen);
      painter.drawRoundedRect(nameBoxRect, 5, 5);
      QRectF nameTextDrawRect = nameBoxRect.adjusted(linePadding, linePadding, -linePadding, -linePadding);
      painter.setPen(Qt::white);
      painter.drawText(nameTextDrawRect, Qt::AlignLeft | Qt::AlignVCenter, nameText);
  }

  // Draw undistortion button if available
  if (m_undistortionAvailable) {
      int boxPadding = 5;
      int iconSize = 32; // Larger icon size for better touch accessibility

      // Position in top-right, adjust if stream name is also there
      int x = width() - iconSize - boxPadding;
      int y = boxPadding;
      if (!m_streamName.isEmpty() && m_streamNameBoxPosition == TopRight) {
          QFontMetrics fm(painter.fontMetrics());
          y += fm.height() + 2 * boxPadding; // Move below stream name box
      }
      
      m_undistortButtonRect = QRect(x, y, iconSize, iconSize);

      // Choose icon based on mode
      QString iconPath;
      switch (m_undistortionMode) {
          case 0:
              iconPath = ":/distorted.svg";
              break;
          case 1:
              iconPath = ":/undistorted_alpha0.svg";
              break;
          case 2:
              iconPath = ":/undistorted_alpha1.svg";
              break;
          default:
              iconPath = ":/distorted.svg";
              break;
      }
      
      QSvgRenderer renderer(iconPath);
      if (renderer.isValid()) {
          renderer.render(&painter, m_undistortButtonRect);
      } else {
          // Fallback to text if icon fails to load
          painter.setPen(Qt::red);
          painter.drawText(m_undistortButtonRect, Qt::AlignCenter, "!");
      }
  }
}

void MyWidget::onTextMessageReceived(const QString &message)
{
    if (m_debugPrint) qDebug() << "[DEBUG] onTextMessageReceived called with:" << message;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) return;

    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();

    if (type == "undistortion_info") {
        m_undistortionAvailable = obj["available"].toBool();
        m_undistortionEnabled = obj["enabled"].toBool();
        m_undistortionMode = obj["mode"].toInt(0); // Default to 0 if not present
        if (m_debugPrint) qDebug() << "[DEBUG] Undistortion available:" << m_undistortionAvailable << "enabled:" << m_undistortionEnabled << "mode:" << m_undistortionMode;
        update();
    } else if (type == "undistortion_state") {
        m_undistortionEnabled = obj["enabled"].toBool();
        m_undistortionMode = obj["mode"].toInt(0); // Default to 0 if not present
        if (m_debugPrint) qDebug() << "[DEBUG] Undistortion state updated to enabled:" << m_undistortionEnabled << "mode:" << m_undistortionMode;
        update();
    }
}

void MyWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_debugPrint) qDebug() << "[DEBUG] Mouse press at" << event->pos() << "button rect:" << m_undistortButtonRect;
    if (m_undistortionAvailable && m_undistortButtonRect.contains(event->pos())) {
        if (m_debugPrint) qDebug() << "[DEBUG] Undistort button clicked via mouse";
        if (m_webSocket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject message;
            message["type"] = "control";
            message["command"] = "toggle_undistortion";
            m_webSocket->sendTextMessage(QJsonDocument(message).toJson(QJsonDocument::Compact));
        }
        event->accept();
    } else {
        event->ignore();
    }
}

bool MyWidget::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd: {
        QTouchEvent *touchEvent = static_cast<QTouchEvent*>(event);
        if (touchEvent->points().size() == 1) {
            const QTouchEvent::TouchPoint &touchPoint = touchEvent->points().first();
            QPointF posF = touchPoint.position();
            QPoint pos = posF.toPoint();
            
            if (m_debugPrint) qDebug() << "[DEBUG] Touch event at" << pos << "button rect:" << m_undistortButtonRect;
            
            if (event->type() == QEvent::TouchEnd && 
                m_undistortionAvailable && 
                m_undistortButtonRect.contains(pos)) {
                
                if (m_debugPrint) qDebug() << "[DEBUG] Undistort button touched";
                if (m_webSocket->state() == QAbstractSocket::ConnectedState) {
                    QJsonObject message;
                    message["type"] = "control";
                    message["command"] = "toggle_undistortion";
                    m_webSocket->sendTextMessage(QJsonDocument(message).toJson(QJsonDocument::Compact));
                }
                event->accept();
                return true;
            }
        }
        // Accept all touch events to prevent them from being converted to mouse events
        event->accept();
        return true;
    }
    default:
        break;
    }
    
    return QWidget::event(event);
}

// Implementation for setTransport, setUdpPort
void MyWidget::setTransport(TransportProtocol protocol) {
    if (m_transport == protocol)
        return;
    m_transport = protocol;
    if (m_debugPrint) qDebug() << "[DEBUG] setTransport called with" << (m_transport == UDP ? "UDP" : "WebSocket");
    if (m_transport == UDP) {
        setupUdpSocket();
    } else {
        closeUdpSocket();
    }
}
void MyWidget::setUdpPort(int port) {
    if (m_udpPort == port)
        return;
    m_udpPort = port;
    if (m_debugPrint) qDebug() << "[DEBUG] setUdpPort called with" << port;
    // Recreate UDP socket if transport is already set to UDP
    if (m_transport == UDP) {
        setupUdpSocket();
    }
}

void MyWidget::setFrameDropRatio(int ratio) {
    if (m_frameDropRatio == ratio)
        return;
    m_frameDropRatio = qMax(1, ratio); // Ensure minimum value of 1
    if (m_debugPrint) qDebug() << "[DEBUG] setFrameDropRatio called with" << m_frameDropRatio;
}

void MyWidget::setupUdpSocket()
{
    closeUdpSocket();
    if (m_udpPort <= 0) {
        if (m_debugPrint) qDebug() << "[DEBUG] setupUdpSocket: Invalid port" << m_udpPort;
        return;
    }
    
    if (m_debugPrint) qDebug() << "[DEBUG] setupUdpSocket: Attempting to bind on port" << m_udpPort;
    
    m_udpSocket = new QUdpSocket(this);
    
    // Try to bind to the specified port
    if (!m_udpSocket->bind(QHostAddress::Any, m_udpPort)) {
        if (m_debugPrint) qDebug() << "[DEBUG] Failed to bind UDP socket on port" << m_udpPort << "Error:" << m_udpSocket->errorString();
        delete m_udpSocket;
        m_udpSocket = nullptr;
        return;
    }
    
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &MyWidget::onUdpDatagramReceived);
    connect(m_udpSocket, QOverload<QAbstractSocket::SocketError>::of(&QUdpSocket::errorOccurred),
            this, [this](QAbstractSocket::SocketError error) {
                if (m_debugPrint) qDebug() << "[DEBUG] UDP socket error:" << error << m_udpSocket->errorString();
            });
    
    if (m_debugPrint) qDebug() << "[DEBUG] UDP socket successfully bound on port" << m_udpPort << "Local address:" << m_udpSocket->localAddress().toString();
}

void MyWidget::closeUdpSocket()
{
    if (m_udpSocket) {
        m_udpSocket->close();
        m_udpSocket->deleteLater();
        m_udpSocket = nullptr;
        if (m_debugPrint) qDebug() << "[DEBUG] UDP socket closed.";
    }
}

QString MyWidget::getLocalIpAddress()
{
    // Get all network interfaces
    QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    
    for (const QNetworkInterface &interface : interfaces) {
        // Skip loopback and inactive interfaces
        if (interface.flags() & QNetworkInterface::IsLoopBack)
            continue;
        if (!(interface.flags() & QNetworkInterface::IsUp))
            continue;
        if (!(interface.flags() & QNetworkInterface::IsRunning))
            continue;
            
        // Get all addresses for this interface
        QList<QNetworkAddressEntry> entries = interface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            QHostAddress addr = entry.ip();
            // Look for IPv4 addresses that are not loopback
            if (addr.protocol() == QAbstractSocket::IPv4Protocol && 
                !addr.isLoopback() && 
                !addr.isMulticast()) {
                
                QString ipStr = addr.toString();
                if (m_debugPrint) qDebug() << "[DEBUG] Found local IP:" << ipStr << "on interface:" << interface.humanReadableName();
                
                // Prefer private network ranges (192.168.x.x, 10.x.x.x, 172.16-31.x.x)
                if (ipStr.startsWith("192.168.") || 
                    ipStr.startsWith("10.") ||
                    (ipStr.startsWith("172.") && ipStr.split('.')[1].toInt() >= 16 && ipStr.split('.')[1].toInt() <= 31)) {
                    return ipStr;
                }
            }
        }
    }
    
    // If no private IP found, try to get any valid IPv4 address
    for (const QNetworkInterface &interface : interfaces) {
        if (interface.flags() & QNetworkInterface::IsLoopBack)
            continue;
        if (!(interface.flags() & QNetworkInterface::IsUp))
            continue;
            
        QList<QNetworkAddressEntry> entries = interface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            QHostAddress addr = entry.ip();
            if (addr.protocol() == QAbstractSocket::IPv4Protocol && 
                !addr.isLoopback()) {
                return addr.toString();
            }
        }
    }
    
    if (m_debugPrint) qDebug() << "[DEBUG] No suitable local IP address found";
    return QString(); // Return empty string if no suitable address found
}

void MyWidget::onUdpDatagramReceived()
{
    if (m_debugPrint) qDebug() << "[DEBUG] onUdpDatagramReceived called";
    
    if (!m_udpSocket) {
        if (m_debugPrint) qDebug() << "[DEBUG] onUdpDatagramReceived: m_udpSocket is null";
        return;
    }
    
    while (m_udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(m_udpSocket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort;
        
        qint64 bytesRead = m_udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        
        if (bytesRead > 0) {
            if (m_debugPrint) qDebug() << "[DEBUG] UDP datagram received from" << sender.toString() << ":" << senderPort << "size:" << bytesRead;
            datagram.resize(int(bytesRead)); // Resize to actual data size
            // Reuse the same logic as WebSocket binary message
            onBinaryMessageReceived(datagram);
        } else {
            if (m_debugPrint) qDebug() << "[DEBUG] Failed to read UDP datagram, error:" << m_udpSocket->errorString();
        }
    }
}

void MyWidget::setStreamName(const QString &name, int position) {
    m_streamName = name;
    if (position >= 1 && position <= 4)
        m_streamNameBoxPosition = static_cast<BoxPosition>(position);
    
    if (m_debugPrint) qDebug() << "[DEBUG] setStreamName called with" << name << ", position:" << m_streamNameBoxPosition;
    update();
}
void MyWidget::setStreamNameBoxPosition(BoxPosition pos) {
    setStreamName(m_streamName, static_cast<int>(pos));
 }

 void MyWidget::setInGedi(bool inGedi)
{
    if (m_inGedi == inGedi)
        return;
    m_inGedi = inGedi;
    update();
}

// Add getters for Q_PROPERTY
QString MyWidget::getStreamName() const { return m_streamName; }
MyWidget::BoxPosition MyWidget::getStreamNameBoxPosition() const { return m_streamNameBoxPosition; }
QString MyWidget::getWebSocketUrl() const { return m_webSocketUrl; }
QString MyWidget::getRtspStreamUrl() const { return m_rtspStreamUrl; }
MyWidget::TransportProtocol MyWidget::getTransport() const { return m_transport; }
int MyWidget::getUdpPort() const { return m_udpPort; }
int MyWidget::getFrameDropRatio() const { return m_frameDropRatio; }
bool MyWidget::isInGedi() const { return m_inGedi; }

//--------------------------------------------------------------------------------
// Here comes the implementation of the EWO interface class
// The EWO interface class is just a wrapper around a normal Qt widget
// and it gives WinCC OA a standard interface to communicate with it.
// In our example we use our own "MyWidget"
//--------------------------------------------------------------------------------

/**
 * \brief streamingEWO::streamingEWO
 * Constructor for the EWO interface class. Wraps the custom widget for WinCC OA.
 * \param parent The parent widget, typically provided by the WinCC OA framework.
 */
streamingEWO::streamingEWO(QWidget *parent)
  : BaseExternWidget(parent)
{
  // the widget will be deleted by the QWidget parent
  // Don't do it in destructor
  baseWidget = new MyWidget(parent);
}

//--------------------------------------------------------------------------------

/**
 * \brief streamingEWO::widget
 * Returns the base widget pointer for WinCC OA integration.
 * \return Pointer to the main QWidget (MyWidget instance).
 */
QWidget *streamingEWO::widget() const
{
  return baseWidget;
}

//--------------------------------------------------------------------------------

/**
 * \brief streamingEWO::signalList
 * Returns the list of signals supported by this EWO (currently empty).
 * \return QStringList of supported signal names (empty in this implementation).
 */
QStringList streamingEWO::signalList() const
{
  QStringList list;

  return list;
}

//--------------------------------------------------------------------------------

/**
 * \brief streamingEWO::methodList
 * Returns the list of methods supported by this EWO.
 * \return QStringList of supported method signatures.
 */
QStringList streamingEWO::methodList() const
{
  QStringList list;

  list.append("void setWebSocketUrl(string url)");
  list.append("void setRtspStreamUrl(string url)");
  list.append("void setDebugMode(bool enabled)"); // Add new method
  list.append("void setDebugPrint(bool enabled)"); // Add new method
  list.append("void setTransport(string transport)");
  list.append("void setUdpPort(int port)"); // Add UDP port method
  list.append("void setFrameDropRatio(int ratio)"); // Add frame drop ratio method
  list.append("void setStreamName(string name, int position=-1)");

  return list;
}

//--------------------------------------------------------------------------------

/**
 * \brief streamingEWO::methodInterface
 * Describes the method interface for WinCC OA integration.
 * \param name The method name as a QString.
 * \param retVal Reference to a QVariant::Type to specify the return type.
 * \param args Reference to a QList<QVariant::Type> to specify argument types.
 * \return True if the method is supported, false otherwise.
 */
bool streamingEWO::methodInterface(const QString &name, QVariant::Type &retVal,
                                 QList<QVariant::Type> &args) const
{
  if ( name == "setWebSocketUrl" )
  {
    retVal = QVariant::Invalid;  // we return void

    // we only have 1 argument, which is a string
    args.append(QVariant::String);
    return true;
  }

  if ( name == "setRtspStreamUrl" )
  {
    retVal = QVariant::Invalid;  // we return void

    // we only have 1 argument, which is a string
    args.append(QVariant::String);
    return true;
  }

  if ( name == "setDebugMode" )
  {
    retVal = QVariant::Invalid;  // we return void
    args.append(QVariant::Bool); // Argument is a boolean
    return true;
  }

  if ( name == "setDebugPrint" )
  {
    retVal = QVariant::Invalid;  // we return void
    args.append(QVariant::Bool); // Argument is a boolean
    return true;
  }

  if ( name == "setTransport" )
  {
    retVal = QVariant::Invalid;
    args.append(QVariant::String);
    return true;
  }
  if ( name == "setUdpPort" )
  {
    retVal = QVariant::Invalid;
    args.append(QVariant::Int);
    return true;
  }
  if ( name == "setFrameDropRatio" )
  {
    retVal = QVariant::Invalid;
    args.append(QVariant::Int);
    return true;
  }
  if ( name == "setStreamName" )
  {
    retVal = QVariant::Invalid;
    args.append(QVariant::String);
    args.append(QVariant::Int); // Optional, but always present in interface
    return true;
  }

  return false;
}

//--------------------------------------------------------------------------------

/**
 * \brief streamingEWO::invokeMethod
 * Invokes a method by name with the given arguments, for WinCC OA integration.
 * \param name The method name as a QString.
 * \param values List of argument values as QVariants.
 * \param error Reference to a QString to receive error messages if invocation fails.
 * \return The result of the method call as a QVariant (or an invalid QVariant for void methods).
 */
QVariant streamingEWO::invokeMethod(const QString &name, QList<QVariant> &values, QString &error)
{
  if ( name == "setWebSocketUrl" )
  {
    if ( !hasNumArgs(name, values, 1, error) ) return QVariant();
    baseWidget->setWebSocketUrl(values[0].toString());
    return QVariant();
  }

  if ( name == "setRtspStreamUrl" )
  {
    if ( !hasNumArgs(name, values, 1, error) ) return QVariant();
    baseWidget->setRtspStreamUrl(values[0].toString());
    return QVariant();
  }

  if ( name == "setDebugMode" )
  {
    if ( !hasNumArgs(name, values, 1, error) ) return QVariant();
    if (values[0].typeId() != QMetaType::Bool) { //Type check
        error = QString("Argument for %1 must be a boolean").arg(name);
        return QVariant();
    }
    baseWidget->setDebugMode(values[0].toBool());
    return QVariant();
  }

  if ( name == "setDebugPrint" )
  {
    if ( !hasNumArgs(name, values, 1, error) ) return QVariant();
    if (values[0].typeId() != QMetaType::Bool) { //Type check
        error = QString("Argument for %1 must be a boolean").arg(name);
        return QVariant();
    }
    baseWidget->setDebugPrint(values[0].toBool());
    return QVariant();
  }

  if ( name == "setTransport" )
  {
    if ( !hasNumArgs(name, values, 1, error) ) return QVariant();
    QString protoStr = values[0].toString().trimmed().toLower();
    MyWidget::TransportProtocol proto = MyWidget::WebSocket;
    if (protoStr == "udp")
      proto = MyWidget::UDP;
    else if (protoStr == "websocket")
      proto = MyWidget::WebSocket;
    else {
      error = QString("Invalid transport protocol: '%1'. Use 'udp' or 'websocket'.").arg(protoStr);
      return QVariant();
    }
    baseWidget->setTransport(proto);
    return QVariant();
  }

  if ( name == "setUdpPort" )
  {
    if ( !hasNumArgs(name, values, 1, error) ) return QVariant();
    baseWidget->setUdpPort(values[0].toInt());
    return QVariant();
  }

  if ( name == "setFrameDropRatio" )
  {
    if ( !hasNumArgs(name, values, 1, error) ) return QVariant();
    baseWidget->setFrameDropRatio(values[0].toInt());
    return QVariant();
  }

  if ( name == "setStreamName" )
  {
    if (values.size() == 1)
      baseWidget->setStreamName(values[0].toString());
    else if (values.size() >= 2)
      baseWidget->setStreamName(values[0].toString(), values[1].toInt());
    return QVariant();
  }

  return BaseExternWidget::invokeMethod(name, values, error);
}