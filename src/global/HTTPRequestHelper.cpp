#include "include/global/HTTPRequestHelper.hpp"

#include <QApplication>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <qeventloop.h>
#include <qnetworkaccessmanager.h>
#include <qnetworkreply.h>
#include <qthread.h>
#include <qtimer.h>

#include "include/global/NekoGui.hpp"

namespace NekoGui_network {

// Shared manager for all requests
static QSharedPointer<QNetworkAccessManager> sharedManager;

NekoHTTPResponse NetworkRequestHelper::HttpGet(const QString &url) {
  QSharedPointer<QNetworkAccessManager> manager;

  if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
    if (!sharedManager) {
      sharedManager.reset(new QNetworkAccessManager);
    }
    manager = sharedManager;
  } else {
    manager.reset(new QNetworkAccessManager);
  }

  QNetworkRequest request;
  request.setUrl(url);

  if (NekoGui::dataStore->sub_use_proxy ||
      NekoGui::dataStore->spmode_system_proxy) {
    if (NekoGui::dataStore->started_id < 0) {
      return NekoHTTPResponse{
          QObject::tr("Request with proxy but no profile started.")};
    }

    QNetworkProxy p;
    p.setType(QNetworkProxy::HttpProxy);
    p.setHostName("127.0.0.1");
    p.setPort(NekoGui::dataStore->inbound_socks_port);
    manager->setProxy(p);
  }

  // SSL
  if (NekoGui::dataStore->sub_insecure) {
    QSslConfiguration c = request.sslConfiguration();
    c.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(c);
  }

  // Set attribute
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    NekoGui::dataStore->GetUserAgent());

  QNetworkReply *reply = manager->get(request);

  QTimer timeoutTimer;
  timeoutTimer.setSingleShot(true);

  QEventLoop loop;
  connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  connect(&timeoutTimer, &QTimer::timeout, [&]() {
    if (reply->isRunning()) {
      reply->abort();
      reply->deleteLater();
    }
  });

  // SSL Errors
  connect(reply, &QNetworkReply::sslErrors, [](const QList<QSslError> &errors) {
    QStringList errorMessages;
    for (const auto &error : errors) {
      errorMessages << error.errorString();
    }
    MW_show_log(QString("SSL Errors: %1 %2")
                    .arg(errorMessages.join(","),
                         NekoGui::dataStore->sub_insecure ? "(Ignored)" : ""));
  });

  // Wait for response
  timeoutTimer.start(10000);
  loop.exec(QEventLoop::ExcludeUserInputEvents);

  NekoHTTPResponse response;

  if (reply->error() != QNetworkReply::NoError) {
    response.error = reply->errorString();
    qWarning() << "HTTP Request failed:" << url << "-" << response.error;
  } else {
    response.data = reply->readAll();
    response.header = reply->rawHeaderPairs();
  }

  // Clear
  reply->deleteLater();
  return response;
}

QString NetworkRequestHelper::GetHeader(
    const QList<QPair<QByteArray, QByteArray>> &header, const QString &name) {
  for (const auto &p : header) {
    if (QString::compare(p.first, name, Qt::CaseInsensitive) == 0) {
      return p.second;
    }
  }
  return "";
}

QString NetworkRequestHelper::DownloadAsset(const QString &url,
                                            const QString &fileName) {
  QNetworkRequest request;
  QNetworkAccessManager accessManager;
  request.setUrl(url);
  if (NekoGui::dataStore->spmode_system_proxy) {
    QNetworkProxy p;
    p.setType(QNetworkProxy::HttpProxy);
    p.setHostName("127.0.0.1");
    p.setPort(NekoGui::dataStore->inbound_socks_port);
    accessManager.setProxy(p);
    if (NekoGui::dataStore->started_id < 0) {
      return QObject::tr("Request with proxy but no profile started.");
    }
  }

  auto _reply = accessManager.get(request);
  connect(_reply, &QNetworkReply::sslErrors, _reply,
          [](const QList<QSslError> &errors) {
            QStringList error_str;
            for (const auto &err : errors) {
              error_str << err.errorString();
            }
            MW_show_log(QString("SSL Errors: %1").arg(error_str.join(",")));
          });
  QEventLoop loop;
  connect(_reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();
  if (_reply->error() != QNetworkReply::NetworkError::NoError) {
    return _reply->errorString();
  }

  auto filePath = NekoGui::GetBasePath() + "/" + fileName;
  auto file = QFile(filePath);
  if (file.exists()) {
    file.remove();
  }
  if (!file.open(QIODevice::WriteOnly)) {
    return QObject::tr("Could not open file.");
  }
  file.write(_reply->readAll());

  file.close();
  return "";
}

} // namespace NekoGui_network
