/*
    Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/
#ifndef QNetworkReplyHandler_h
#define QNetworkReplyHandler_h

#include <QObject>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QBasicTimer>

#include "SecurityOrigin.h"
#include "FormData.h"
#include "QtMIMETypeSniffer.h"

QT_BEGIN_NAMESPACE
class QFile;
class QNetworkReply;
QT_END_NAMESPACE

namespace WebCore {

class FormDataIODevice;
class ResourceError;
class ResourceHandle;
class ResourceRequest;
class ResourceResponse;
class QNetworkReplyHandler;

class QNetworkReplyHandlerCallQueue final : public QObject {
    Q_OBJECT
public:
    QNetworkReplyHandlerCallQueue(QNetworkReplyHandler*, bool deferSignals);
    bool deferSignals() const { return m_deferSignals; }
    void setDeferSignals(bool, bool sync = false);

    typedef void (QNetworkReplyHandler::*EnqueuedCall)();
    void push(EnqueuedCall method);
    void requeue(EnqueuedCall method);
    void clear() { m_enqueuedCalls.clear(); }

    void lock();
    void unlock();
private:
    QNetworkReplyHandler* m_replyHandler;
    int m_locks;
    bool m_deferSignals;
    bool m_flushing;
    QList<EnqueuedCall> m_enqueuedCalls;

    Q_INVOKABLE void flush();
};

class QNetworkReplyWrapper final : public QObject {
    Q_OBJECT
public:
    QNetworkReplyWrapper(QNetworkReplyHandlerCallQueue*, QNetworkReply*, bool sniffMIMETypes, QObject* parent = 0);
    ~QNetworkReplyWrapper();

    QNetworkReply* reply() const { return m_reply; }
    QNetworkReply* release();

    void synchronousLoad();

    QUrl redirectionTargetUrl() const { return m_redirectionTargetUrl; }
    QString encoding() const { return m_encoding; }
    QString advertisedMIMEType() const { return m_advertisedMIMEType; }
    QString mimeType() const { return m_sniffedMIMEType.isEmpty() ? m_advertisedMIMEType : m_sniffedMIMEType; }

    bool responseContainsData() const { return m_responseContainsData; }
    bool wasRedirected() const { return m_redirectionTargetUrl.isValid(); }

    // See setFinished().
    bool isFinished() const { return m_reply->property("_q_isFinished").toBool(); }

private Q_SLOTS:
    void receiveMetaData();
    void didReceiveFinished();
    void didReceiveReadyRead();
    void receiveSniffedMIMEType();
    void setFinished();
    void replyDestroyed();

private:
    void stopForwarding();
    void emitMetaDataChanged();

    QNetworkReply* m_reply;
    QUrl m_redirectionTargetUrl;

    QString m_encoding;
    QNetworkReplyHandlerCallQueue* m_queue;
    bool m_responseContainsData;

    QString m_advertisedMIMEType;

    QString m_sniffedMIMEType;
    std::unique_ptr<QtMIMETypeSniffer> m_sniffer;
    bool m_sniffMIMETypes;
};

class QNetworkReplyHandler final : public QObject
{
    Q_OBJECT
public:
    enum LoadType {
        AsynchronousLoad,
        SynchronousLoad
    };

    QNetworkReplyHandler(ResourceHandle*, LoadType, bool deferred = false);
    void setLoadingDeferred(bool deferred) { m_queue.setDeferSignals(deferred, m_loadType == SynchronousLoad); }

    QNetworkReply* reply() const { return m_replyWrapper ? m_replyWrapper->reply() : 0; }

    void abort();

    QNetworkReply* release();

    void finish();
    void forwardData();
    void sendResponseIfNeeded();

    void continueWillSendRequest(ResourceRequest&&);
    void continueDidReceiveResponse();

    static ResourceError errorForReply(QNetworkReply*);

private Q_SLOTS:
    void uploadProgress(qint64 bytesSent, qint64 bytesTotal);

private:
    void start();
    String httpMethod() const;
    void continueAfterWillSendRequest(ResourceRequest&&);
    void redirect(ResourceResponse&, const QUrl&);
    bool wasAborted() const { return !m_resourceHandle; }
    QNetworkReply* sendNetworkRequest(QNetworkAccessManager*, const ResourceRequest&);
    FormDataIODevice* getIODevice(const ResourceRequest&);
    void clearContentHeaders();
    void timerEvent(QTimerEvent*) final;
    void timeout();

    std::unique_ptr<QNetworkReplyWrapper> m_replyWrapper;
    ResourceHandle* m_resourceHandle;
    LoadType m_loadType;
    QNetworkAccessManager::Operation m_method;
    QNetworkRequest m_request;
    QBasicTimer m_timeoutTimer;

    // defer state holding
    int m_redirectionTries;

    QNetworkReplyHandlerCallQueue m_queue;
};

// Self destructing QIODevice for FormData
//  For QNetworkAccessManager::put we will have to gurantee that the
//  QIODevice is valid as long finished() of the QNetworkReply has not
//  been emitted. With the presence of QNetworkReplyHandler::release I do
//  not want to gurantee this.
class FormDataIODevice final : public QIODevice {
    Q_OBJECT
public:
    FormDataIODevice(const RefPtr<FormData>&);
    ~FormDataIODevice();

    bool isSequential() const final;
    qint64 getFormDataSize() const { return m_fileSize + m_dataSize; }
    bool reset() final;

protected:
    qint64 readData(char*, qint64) final;
    qint64 writeData(const char*, qint64) final;

private:
    void prepareFormElements();
    void moveToNextElement();
    qint64 computeSize();
    void openFileForCurrentElement(const FormDataElement::EncodedFileData&);
    void prepareCurrentElement();

private:
    Vector<FormDataElement> m_formElements;
    QFile* m_currentFile;
    qint64 m_currentDelta;
    qint64 m_fileSize;
    qint64 m_dataSize;
    RefPtr<FormData> m_formData;
};

}

#endif // QNetworkReplyHandler_h
