/*
    Copyright (C) 2015 The Qt Company Ltd
    Copyright (C) 2007 Staikos Computing Services Inc.  <info@staikos.net>
    Copyright (C) 2008 Holger Hans Peter Freyther

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
#include "config.h"
#include "QNetworkReplyHandler.h"

#include "BlobData.h"
#include "BlobRegistry.h"
#include "HTTPParsers.h"
#include "MIMETypeRegistry.h"
#include "NetworkingContext.h"
#include "ResourceHandle.h"
#include "ResourceHandleClient.h"
#include "ResourceHandleInternal.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "SharedBuffer.h"
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QNetworkCookie>
#include <QNetworkReply>

#include <wtf/text/CString.h>

#include <QCoreApplication>

#if USE(HTTP2) && QT_VERSION < QT_VERSION_CHECK(5, 14, 0)

#include <private/http2protocol_p.h>
#include <cstdlib>

// Redefine private bits which are not currenly exported from QtNetwork

QT_BEGIN_NAMESPACE

namespace Http2 {
const char *http2ParametersPropertyName = "QT_HTTP2_PARAMETERS_PROPERTY";

ProtocolParameters::ProtocolParameters()
{
    settingsFrameData[Settings::INITIAL_WINDOW_SIZE_ID] = qtDefaultStreamReceiveWindowSize;
    settingsFrameData[Settings::ENABLE_PUSH_ID] = 0;
}
}

QT_END_NAMESPACE

#endif // USE(HTTP2) && QT_VERSION < QT_VERSION_CHECK(5, 14, 0)

static const int gMaxRedirections = 10;

namespace WebCore {

FormDataIODevice::FormDataIODevice(const RefPtr<FormData>& data)
    : m_currentFile(0)
    , m_currentDelta(0)
    , m_fileSize(0)
    , m_dataSize(0)
    , m_formData(data)
{
    setOpenMode(FormDataIODevice::ReadOnly);

    prepareFormElements();
    prepareCurrentElement();
    computeSize();
}

FormDataIODevice::~FormDataIODevice()
{
    delete m_currentFile;
}

bool FormDataIODevice::reset()
{
    if (m_currentFile)
        m_currentFile->close();

    m_currentDelta = 0;

    if (m_formData)
        m_formElements = m_formData->elements();
    else
        m_formElements.clear();

    prepareCurrentElement();
    return true;
}

void FormDataIODevice::prepareFormElements()
{
    if (!m_formData)
        return;

    m_formData = m_formData->resolveBlobReferences(blobRegistry().blobRegistryImpl());

    // Take a deep copy of the FormDataElements
    m_formElements = m_formData->elements();
}


qint64 FormDataIODevice::computeSize()
{
    for (const auto& element : m_formElements) {
        switchOn(element.data,
            [&] (const Vector<uint8_t>& bytes) {
                m_dataSize += bytes.size();
            }, [&] (const FormDataElement::EncodedFileData& fileData) {
                QFileInfo fi(fileData.filename);
                qint64 fileEnd = fi.size();
                if (fileData.fileLength != BlobDataItem::toEndOfFile)
                    fileEnd = qMin<qint64>(fi.size(), fileData.fileStart + fileData.fileLength);
                m_fileSize += qMax<qint64>(0, fileEnd - fileData.fileStart);
            }, [&] (const FormDataElement::EncodedBlobData&) {
                // QTFIXME
            });
    }
    return m_dataSize + m_fileSize;
}

void FormDataIODevice::moveToNextElement()
{
    if (m_currentFile)
        m_currentFile->close();
    m_currentDelta = 0;

    m_formElements.remove(0);

    prepareCurrentElement();
}

void FormDataIODevice::prepareCurrentElement()
{
    if (m_formElements.isEmpty())
        return;

    switchOn(m_formElements[0].data,
        [&] (const Vector<uint8_t>&) {
            // Nothing to do
        }, [&] (const FormDataElement::EncodedFileData& fileData) {
            openFileForCurrentElement(fileData);
        }, [&] (const FormDataElement::EncodedBlobData&) {
            // At this point encodedBlob should already have been handled.
            ASSERT_NOT_REACHED();
    });
}

void FormDataIODevice::openFileForCurrentElement(const FormDataElement::EncodedFileData& fileData)
{
    if (!m_currentFile)
        m_currentFile = new QFile;

    m_currentFile->setFileName(fileData.filename);
    m_currentFile->open(QFile::ReadOnly);
    if (fileData.expectedFileModificationTime) {
        QFileInfo info(*m_currentFile);
        if (!info.exists() || (fileData.expectedFileModificationTime.value().secondsSinceEpoch().milliseconds() < info.lastModified().toMSecsSinceEpoch())) {
            moveToNextElement();
            return;
        }
    }
    if (fileData.fileStart)
        m_currentFile->seek(fileData.fileStart);
}

// m_formElements[0] is the current item. If the destination buffer is
// big enough we are going to read from more than one FormDataElement
qint64 FormDataIODevice::readData(char* destination, qint64 size)
{
    if (m_formElements.isEmpty())
        return -1;

    qint64 copied = 0;
    while (copied < size && !m_formElements.isEmpty()) {
        const qint64 available = size - copied;

        switchOn(m_formElements[0].data,
            [&] (const Vector<uint8_t>& bytes) {
                const qint64 toCopy = qMin<qint64>(available, bytes.size() - m_currentDelta);
                memcpy(destination + copied, bytes.data()+m_currentDelta, toCopy);
                m_currentDelta += toCopy;
                copied += toCopy;

                if (m_currentDelta == bytes.size())
                    moveToNextElement();
            }, [&] (const FormDataElement::EncodedFileData& fileData) {
                quint64 toCopy = available;
                if (fileData.fileLength != BlobDataItem::toEndOfFile)
                    toCopy = qMin<qint64>(toCopy, fileData.fileLength - m_currentDelta);
                const QByteArray data = m_currentFile->read(toCopy);
                memcpy(destination+copied, data.constData(), data.size());
                m_currentDelta += data.size();
                copied += data.size();

                if (m_currentFile->atEnd() || !m_currentFile->isOpen())
                    moveToNextElement();
                else if (fileData.fileLength != BlobDataItem::toEndOfFile && m_currentDelta == fileData.fileLength)
                    moveToNextElement();
            }, [&] (const FormDataElement::EncodedBlobData&) {
                // QTFIXME
            });
    }

    return copied;
}

qint64 FormDataIODevice::writeData(const char*, qint64)
{
    return -1;
}

bool FormDataIODevice::isSequential() const
{
    return true;
}

QNetworkReplyHandlerCallQueue::QNetworkReplyHandlerCallQueue(QNetworkReplyHandler* handler, bool deferSignals)
    : m_replyHandler(handler)
    , m_locks(0)
    , m_deferSignals(deferSignals)
    , m_flushing(false)
{
    Q_ASSERT(handler);
}

void QNetworkReplyHandlerCallQueue::push(EnqueuedCall method)
{
    m_enqueuedCalls.append(method);
    flush();
}

void QNetworkReplyHandlerCallQueue::requeue(EnqueuedCall method)
{
    m_enqueuedCalls.prepend(method);
    flush();
}

void QNetworkReplyHandlerCallQueue::lock()
{
    ++m_locks;
}

void QNetworkReplyHandlerCallQueue::unlock()
{
    if (!m_locks)
        return;

    --m_locks;
    flush();
}

void QNetworkReplyHandlerCallQueue::setDeferSignals(bool defer, bool sync)
{
    m_deferSignals = defer;
    if (sync)
        flush();
    else
        QMetaObject::invokeMethod(this, "flush",  Qt::QueuedConnection);
}

void QNetworkReplyHandlerCallQueue::flush()
{
    if (m_flushing)
        return;

    m_flushing = true;

    while (!m_deferSignals && !m_locks && !m_enqueuedCalls.isEmpty())
        (m_replyHandler->*(m_enqueuedCalls.takeFirst()))();

    m_flushing = false;
}

class QueueLocker {
public:
    QueueLocker(QNetworkReplyHandlerCallQueue* queue) : m_queue(queue) { m_queue->lock(); }
    ~QueueLocker() { m_queue->unlock(); }
private:
    QNetworkReplyHandlerCallQueue* m_queue;
};

QNetworkReplyWrapper::QNetworkReplyWrapper(QNetworkReplyHandlerCallQueue* queue, QNetworkReply* reply, bool sniffMIMETypes, QObject* parent)
    : QObject(parent)
    , m_reply(reply)
    , m_queue(queue)
    , m_responseContainsData(false)
    , m_sniffMIMETypes(sniffMIMETypes)
{
    Q_ASSERT(m_reply);

    // setFinished() must be the first that we connect, so isFinished() is updated when running other slots.
    connect(m_reply, SIGNAL(finished()), this, SLOT(setFinished()));
    connect(m_reply, SIGNAL(finished()), this, SLOT(receiveMetaData()));
    connect(m_reply, SIGNAL(readyRead()), this, SLOT(receiveMetaData()));
    connect(m_reply, SIGNAL(destroyed()), this, SLOT(replyDestroyed()));
}

QNetworkReplyWrapper::~QNetworkReplyWrapper()
{
    if (m_reply)
        m_reply->deleteLater();
    m_queue->clear();
}

QNetworkReply* QNetworkReplyWrapper::release()
{
    if (!m_reply)
        return 0;

    m_reply->disconnect(this);
    QNetworkReply* reply = m_reply;
    m_reply = 0;
    m_sniffer = nullptr;

    return reply;
}

void QNetworkReplyWrapper::synchronousLoad()
{
    setFinished();
    receiveMetaData();
}

void QNetworkReplyWrapper::stopForwarding()
{
    if (m_reply) {
        // Disconnect all connections that might affect the ResourceHandleClient.
        m_reply->disconnect(this, SLOT(receiveMetaData()));
        m_reply->disconnect(this, SLOT(didReceiveFinished()));
        m_reply->disconnect(this, SLOT(didReceiveReadyRead()));
    }
    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);
}

void QNetworkReplyWrapper::receiveMetaData()
{
    // This slot is only used to receive the first signal from the QNetworkReply object.
    stopForwarding();

    WTF::String contentType = m_reply->header(QNetworkRequest::ContentTypeHeader).toString();
    m_encoding = extractCharsetFromMediaType(contentType).toString();
    m_advertisedMIMEType = extractMIMETypeFromMediaType(contentType);

    m_redirectionTargetUrl = m_reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (m_redirectionTargetUrl.isValid()) {
        QueueLocker lock(m_queue);
        m_queue->push(&QNetworkReplyHandler::sendResponseIfNeeded);
        m_queue->push(&QNetworkReplyHandler::finish);
        return;
    }

    if (!m_sniffMIMETypes) {
        emitMetaDataChanged();
        return;
    }

    bool isSupportedImageType = MIMETypeRegistry::isSupportedImageMIMEType(m_advertisedMIMEType);

    Q_ASSERT(!m_sniffer);

    m_sniffer = std::make_unique<QtMIMETypeSniffer>(m_reply, m_advertisedMIMEType, isSupportedImageType);

    if (m_sniffer->isFinished()) {
        receiveSniffedMIMEType();
        return;
    }

    connect(m_sniffer.get(), SIGNAL(finished()), this, SLOT(receiveSniffedMIMEType()));
}

void QNetworkReplyWrapper::receiveSniffedMIMEType()
{
    Q_ASSERT(m_sniffer);

    m_sniffedMIMEType = m_sniffer->mimeType();
    m_sniffer = nullptr;

    emitMetaDataChanged();
}

void QNetworkReplyWrapper::setFinished()
{
    // Due to a limitation of QNetworkReply public API, its subclasses never get the chance to
    // change the result of QNetworkReply::isFinished() method. So we need to keep track of the
    // finished state ourselves. This limitation is fixed in 4.8, but we'll still have applications
    // that don't use the solution. See http://bugreports.qt.nokia.com/browse/QTBUG-11737.
    Q_ASSERT(!isFinished());
    m_reply->setProperty("_q_isFinished", true);
}

void QNetworkReplyWrapper::replyDestroyed()
{
    m_reply = 0;
    m_sniffer = nullptr;
}

void QNetworkReplyWrapper::emitMetaDataChanged()
{
    QueueLocker lock(m_queue);
    m_queue->push(&QNetworkReplyHandler::sendResponseIfNeeded);

    if (m_reply->bytesAvailable()) {
        m_responseContainsData = true;
        m_queue->push(&QNetworkReplyHandler::forwardData);
    }

    if (isFinished()) {
        m_queue->push(&QNetworkReplyHandler::finish);
        return;
    }

    // If not finished, connect to the slots that will be used from this point on.
    connect(m_reply, SIGNAL(readyRead()), this, SLOT(didReceiveReadyRead()));
    connect(m_reply, SIGNAL(finished()), this, SLOT(didReceiveFinished()));
}

void QNetworkReplyWrapper::didReceiveReadyRead()
{
    if (m_reply->bytesAvailable())
        m_responseContainsData = true;
    m_queue->push(&QNetworkReplyHandler::forwardData);
}

void QNetworkReplyWrapper::didReceiveFinished()
{
    // Disconnecting will make sure that nothing will happen after emitting the finished signal.
    stopForwarding();
    m_queue->push(&QNetworkReplyHandler::finish);
}

String QNetworkReplyHandler::httpMethod() const
{
    switch (m_method) {
    case QNetworkAccessManager::GetOperation:
        return "GET"_s;
    case QNetworkAccessManager::HeadOperation:
        return "HEAD"_s;
    case QNetworkAccessManager::PostOperation:
        return "POST"_s;
    case QNetworkAccessManager::PutOperation:
        return "PUT"_s;
    case QNetworkAccessManager::DeleteOperation:
        return "DELETE"_s;
    case QNetworkAccessManager::CustomOperation:
        return String(m_resourceHandle->firstRequest().httpMethod());
    default:
        ASSERT_NOT_REACHED();
        return "GET"_s;
    }
}

QNetworkReplyHandler::QNetworkReplyHandler(ResourceHandle* handle, LoadType loadType, bool deferred)
    : QObject(0)
    , m_resourceHandle(handle)
    , m_loadType(loadType)
    , m_redirectionTries(gMaxRedirections)
    , m_queue(this, deferred)
{
    const ResourceRequest &r = m_resourceHandle->firstRequest();

    if (r.httpMethod() == "GET"_s)
        m_method = QNetworkAccessManager::GetOperation;
    else if (r.httpMethod() == "HEAD"_s)
        m_method = QNetworkAccessManager::HeadOperation;
    else if (r.httpMethod() == "POST"_s)
        m_method = QNetworkAccessManager::PostOperation;
    else if (r.httpMethod() == "PUT"_s)
        m_method = QNetworkAccessManager::PutOperation;
    else if (r.httpMethod() == "DELETE"_s && !r.httpBody()) // A delete with a body is a custom operation.
        m_method = QNetworkAccessManager::DeleteOperation;
    else
        m_method = QNetworkAccessManager::CustomOperation;

    m_request = r.toNetworkRequest(m_resourceHandle->getInternal()->m_context.get());

    m_queue.push(&QNetworkReplyHandler::start);
}

void QNetworkReplyHandler::abort()
{
    m_resourceHandle = 0;
    if (QNetworkReply* reply = release()) {
        reply->abort();
        reply->deleteLater();
    }
    deleteLater();
}

QNetworkReply* QNetworkReplyHandler::release()
{
    if (!m_replyWrapper)
        return 0;

    m_timeoutTimer.stop();
    QNetworkReply* reply = m_replyWrapper->release();
    m_replyWrapper = nullptr;
    return reply;
}

static bool shouldIgnoreHttpError(QNetworkReply* reply, bool receivedData)
{
    int httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // Don't ignore error if we haven't received HTTP status code
    if (httpStatusCode == 0)
        return false;

    // An HEAD XmlHTTPRequest shouldn't be marked as failure for HTTP errors.
    if (reply->operation() == QNetworkAccessManager::HeadOperation)
        return true;

    if (httpStatusCode == 401 || httpStatusCode == 407)
        return true;

    if (receivedData && (httpStatusCode >= 400 && httpStatusCode < 600))
        return true;

    return false;
}

void QNetworkReplyHandler::finish()
{
    ASSERT(m_replyWrapper && m_replyWrapper->reply() && !wasAborted());
    m_timeoutTimer.stop();

    ResourceHandleClient* client = m_resourceHandle->client();
    if (!client) {
        m_replyWrapper = nullptr;
        return;
    }

    if (m_replyWrapper->wasRedirected()) {
        m_replyWrapper = nullptr;
        m_queue.push(&QNetworkReplyHandler::start);
        return;
    }

    if (!m_replyWrapper->reply()->error() || shouldIgnoreHttpError(m_replyWrapper->reply(), m_replyWrapper->responseContainsData()))
        // QTFIXME: pass resource metrics?
        client->didFinishLoading(m_resourceHandle, { });
    else
        client->didFail(m_resourceHandle, errorForReply(m_replyWrapper->reply()));

    m_replyWrapper = nullptr;
}

void QNetworkReplyHandler::timeout()
{
    if (!m_replyWrapper || wasAborted())
        return;

    // The request is already finished, but is probably just waiting in the queue to get processed.
    // In this case we ignore the timeout and proceed as normal.
    if (m_replyWrapper->isFinished())
        return;

    ResourceHandleClient* client = m_resourceHandle->client();
    if (!client) {
        m_replyWrapper = nullptr;
        return;
    }

    ASSERT(m_replyWrapper->reply());

    ResourceError timeoutError("QtNetwork"_s, QNetworkReply::TimeoutError, URL(String(m_replyWrapper->reply()->url().toString())), "Request timed out"_s);
    timeoutError.setType(ResourceErrorBase::Type::Timeout);
    client->didFail(m_resourceHandle, timeoutError);

    m_replyWrapper = nullptr;
}

void QNetworkReplyHandler::timerEvent(QTimerEvent* timerEvent)
{
    ASSERT_UNUSED(timerEvent, timerEvent->timerId()== m_timeoutTimer.timerId());
    m_timeoutTimer.stop();
    timeout();
}

void QNetworkReplyHandler::sendResponseIfNeeded()
{
    ASSERT(m_replyWrapper && m_replyWrapper->reply() && !wasAborted());
    ASSERT(!m_queue.deferSignals());

    if (m_replyWrapper->reply()->error() && m_replyWrapper->reply()->attribute(QNetworkRequest::HttpStatusCodeAttribute).isNull())
        return;

    ResourceHandleClient* client = m_resourceHandle->client();
    if (!client)
        return;

    WTF::String mimeType = m_replyWrapper->mimeType();

    if (mimeType.isEmpty()) {
        // let's try to guess from the extension
        mimeType = MIMETypeRegistry::mimeTypeForPath(String(m_replyWrapper->reply()->url().path()));
    }

    URL url(m_replyWrapper->reply()->url());
    // FIXME: we might not need convertToASCIILowercase() because MIME types should always be matched case-insensitively
    ResourceResponse response(url, mimeType.convertToASCIILowercase(),
                              m_replyWrapper->reply()->header(QNetworkRequest::ContentLengthHeader).toLongLong(),
                              m_replyWrapper->encoding());


    if (url.protocolIsInHTTPFamily()) {
        // The status code is equal to 0 for protocols not in the HTTP family.
        int statusCode = m_replyWrapper->reply()->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        response.setHTTPStatusCode(statusCode);
        response.setHTTPStatusText(AtomString::fromLatin1(m_replyWrapper->reply()->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toByteArray().constData()));

        // Add remaining headers. Qt may send a header with multiple values newline-separated; replace with comma.
        foreach (const QNetworkReply::RawHeaderPair& pair, m_replyWrapper->reply()->rawHeaderPairs())
            response.setHTTPHeaderField(String(pair.first.constData(), pair.first.size()), makeStringByReplacingAll(StringView(pair.second.constData(), pair.second.size()), '\n', ','));
    }

    // Note: Qt sets RedirectionTargetAttribute only for 3xx responses, so Location header in 201 responce won't affect this code
    QUrl redirection = m_replyWrapper->reply()->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (redirection.isValid()) {
        redirect(response, redirection);
        return;
    }

    setLoadingDeferred(true);
    client->didReceiveResponseAsync(m_resourceHandle, WTFMove(response), [this]() {
        continueDidReceiveResponse();
    });
}

void QNetworkReplyHandler::continueAfterWillSendRequest(ResourceRequest&& newRequest)
{
    if (wasAborted()) // Network error cancelled the request.
        return;

    m_request = WTFMove(newRequest).toNetworkRequest(m_resourceHandle->getInternal()->m_context.get());
}

void QNetworkReplyHandler::continueWillSendRequest(ResourceRequest&& newRequest)
{
//    ASSERT(!m_resourceHandle->client() || m_resourceHandle->client()->usesAsyncCallbacks());
    ASSERT(m_queue.deferSignals());
    setLoadingDeferred(false);

    continueAfterWillSendRequest(WTFMove(newRequest));
}

void QNetworkReplyHandler::continueDidReceiveResponse()
{
//    ASSERT(!m_resourceHandle->client() || m_resourceHandle->client()->usesAsyncCallbacks());
    ASSERT(m_queue.deferSignals());
    setLoadingDeferred(false);
}

void QNetworkReplyHandler::redirect(ResourceResponse& response, const QUrl& redirection)
{
    ASSERT(!m_queue.deferSignals());

    QUrl currentUrl = m_replyWrapper->reply()->url();

    // RFC7231 section 7.1.2
    QUrl newUrl = currentUrl.resolved(redirection);
    if (!newUrl.hasFragment() && currentUrl.hasFragment())
        newUrl.setFragment(currentUrl.fragment());

    ResourceHandleClient* client = m_resourceHandle->client();
    ASSERT(client);

    int statusCode = m_replyWrapper->reply()->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    m_redirectionTries--;
    if (!m_redirectionTries) {
        ResourceError error("HTTP"_s, 400 /*bad request*/,
                            URL(String(newUrl.toString())),
                            String(QCoreApplication::translate("QWebPage", "Redirection limit reached")));
        client->didFail(m_resourceHandle, error);
        m_replyWrapper = nullptr;
        return;
    }

    //  Status Code 301 (Moved Permanently), 302 (Moved Temporarily), 303 (See Other):
    //    - If original request is POST convert to GET and redirect automatically
    //  Status Code 307 (Temporary Redirect) and all other redirect status codes:
    //    - Use the HTTP method from the previous request
    if ((statusCode >= 301 && statusCode <= 303) && m_resourceHandle->firstRequest().httpMethod() == "POST"_s)
        m_method = QNetworkAccessManager::GetOperation;

    ResourceRequest newRequest = m_resourceHandle->firstRequest();
    newRequest.setHTTPMethod(httpMethod());
    newRequest.setURL(newUrl);

    // Should not set Referer after a redirect from a secure resource to non-secure one.
    if (!newRequest.url().protocolIs("https"_s) && protocolIs(newRequest.httpReferrer(), "https"_s) && m_resourceHandle->context()->shouldClearReferrerOnHTTPSToHTTPRedirect())
        newRequest.clearHTTPReferrer();

    setLoadingDeferred(true);
    client->willSendRequestAsync(m_resourceHandle, WTFMove(newRequest), WTFMove(response), [this](ResourceRequest&& request) {
        continueWillSendRequest(WTFMove(request));
    });
}

void QNetworkReplyHandler::forwardData()
{
    ASSERT(m_replyWrapper && m_replyWrapper->reply() && !wasAborted() && !m_replyWrapper->wasRedirected());

    // reply may be closed if reply->close() or reply->abort() is called
    if (!m_replyWrapper->reply()->isReadable())
        return;

    ResourceHandleClient* client = m_resourceHandle->client();
    if (!client)
        return;

    // We have to use didReceiveBuffer instead of didReceiveData
    // See https://bugs.webkit.org/show_bug.cgi?id=118598
    // and https://bugs.webkit.org/show_bug.cgi?id=118448#c32
    // NetworkResourceLoader implements only didReceiveBuffer and sends it over IPC to WebProcess

    // See also https://codereview.qt-project.org/#/c/79565/
    //
    // FIXME: We need API to get unflattened array of data segments to convert it to non-contiguous SharedBuffer

    qint64 bytesAvailable = m_replyWrapper->reply()->bytesAvailable();
    Vector<uint8_t> buffer(8128); // smaller than 8192 to fit within 8k including overhead.
    while (bytesAvailable > 0 && !m_queue.deferSignals()) {
        qint64 readSize = m_replyWrapper->reply()->read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        if (readSize <= 0)
            break;
        buffer.shrink(readSize);
        bytesAvailable -= readSize;

        // FIXME: https://bugs.webkit.org/show_bug.cgi?id=19793
        // -1 means we do not provide any data about transfer size to inspector so it would use
        // Content-Length headers or content size to show transfer size.
        client->didReceiveBuffer(m_resourceHandle, SharedBuffer::create(WTFMove(buffer)), -1);
        // Check if the request has been aborted or this reply-handler was otherwise released.
        if (wasAborted() || !m_replyWrapper)
            break;
    }
    if (bytesAvailable > 0 && m_replyWrapper)
        m_queue.requeue(&QNetworkReplyHandler::forwardData);
}

void QNetworkReplyHandler::uploadProgress(qint64 bytesSent, qint64 bytesTotal)
{
    if (wasAborted())
        return;

    ResourceHandleClient* client = m_resourceHandle->client();
    if (!client)
        return;

    if (!bytesTotal) {
        // When finished QNetworkReply emits a progress of 0 bytes.
        // Ignore that, to avoid firing twice.
        return;
    }

    client->didSendData(m_resourceHandle, bytesSent, bytesTotal);
}

void QNetworkReplyHandler::clearContentHeaders()
{
    // Clearing Content-length and Content-type of the requests that do not have contents.
    // This is necessary to ensure POST requests redirected to GETs do not leak metadata
    // about the POST content to the site they've been redirected to.
    m_request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant());
    m_request.setHeader(QNetworkRequest::ContentLengthHeader, QVariant());
}

FormDataIODevice* QNetworkReplyHandler::getIODevice(const ResourceRequest& request)
{
    FormDataIODevice* device = new FormDataIODevice(request.httpBody());
    // We may be uploading files so prevent QNR from buffering data.
    m_request.setHeader(QNetworkRequest::ContentLengthHeader, device->getFormDataSize());
    m_request.setAttribute(QNetworkRequest::DoNotBufferUploadDataAttribute, QVariant(true));
    return device;
}

QNetworkReply* QNetworkReplyHandler::sendNetworkRequest(QNetworkAccessManager* manager, const ResourceRequest& request)
{
    if (m_loadType == SynchronousLoad)
        m_request.setAttribute(QNetworkRequest::SynchronousRequestAttribute, true);

    if (!manager)
        return 0;

#if USE(HTTP2) && QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
    static const bool alpnIsSupported = ResourceRequest::alpnIsSupported();
    if (alpnIsSupported && !manager->property(Http2::http2ParametersPropertyName).isValid()) {
        Http2::ProtocolParameters params;
        // QTBUG-77308
        params.maxSessionReceiveWindowSize = Http2::maxSessionReceiveWindowSize / 2;
        // Enable HTTP/2 push
        params.settingsFrameData[Http2::Settings::ENABLE_PUSH_ID] = 1;
        manager->setProperty(Http2::http2ParametersPropertyName, QVariant::fromValue(params));
    }
#endif

    const QUrl url = m_request.url();

    // Post requests on files and data don't really make sense, but for
    // fast/forms/form-post-urlencoded.html and for fast/forms/button-state-restore.html
    // we still need to retrieve the file/data, which means we map it to a Get instead.
    if (m_method == QNetworkAccessManager::PostOperation
        && (!url.toLocalFile().isEmpty() || url.scheme() == QLatin1String("data")))
        m_method = QNetworkAccessManager::GetOperation;

    switch (m_method) {
        case QNetworkAccessManager::GetOperation:
            clearContentHeaders();
            return manager->get(m_request);
        case QNetworkAccessManager::PostOperation: {
            FormDataIODevice* postDevice = getIODevice(request);
            QNetworkReply* result = manager->post(m_request, postDevice);
            postDevice->setParent(result);
            return result;
        }
        case QNetworkAccessManager::HeadOperation:
            clearContentHeaders();
            return manager->head(m_request);
        case QNetworkAccessManager::PutOperation: {
            FormDataIODevice* putDevice = getIODevice(request);
            QNetworkReply* result = manager->put(m_request, putDevice);
            putDevice->setParent(result);
            return result;
        }
        case QNetworkAccessManager::DeleteOperation: {
            clearContentHeaders();
            return manager->deleteResource(m_request);
        }
        case QNetworkAccessManager::CustomOperation: {
            FormDataIODevice* customDevice = getIODevice(request);
            QNetworkReply* result = manager->sendCustomRequest(m_request, m_resourceHandle->firstRequest().httpMethod().latin1().data(), customDevice);
            customDevice->setParent(result);
            return result;
        }
        case QNetworkAccessManager::UnknownOperation:
            ASSERT_NOT_REACHED();
            return 0;
    }
    return 0;
}

void QNetworkReplyHandler::start()
{
    ResourceHandleInternal* d = m_resourceHandle->getInternal();
    if (!d || !d->m_context)
        return;

    QNetworkReply* reply = sendNetworkRequest(d->m_context->networkAccessManager(), d->m_firstRequest);
    if (!reply)
        return;

    m_replyWrapper = std::make_unique<QNetworkReplyWrapper>(&m_queue, reply, m_resourceHandle->shouldContentSniff() && d->m_context->mimeSniffingEnabled(), this);

    if (m_loadType == SynchronousLoad) {
        m_replyWrapper->synchronousLoad();
        // If supported, a synchronous request will be finished at this point, no need to hook up the signals.
        return;
    }

    double timeoutInSeconds = d->m_firstRequest.timeoutInterval();
    if (timeoutInSeconds > 0 && timeoutInSeconds < (INT_MAX / 1000))
        m_timeoutTimer.start(timeoutInSeconds * 1000, this);

//    if (m_resourceHandle->firstRequest().reportUploadProgress())
        connect(m_replyWrapper->reply(), SIGNAL(uploadProgress(qint64, qint64)), this, SLOT(uploadProgress(qint64, qint64)));
}

ResourceError QNetworkReplyHandler::errorForReply(QNetworkReply* reply)
{
    QUrl url = reply->url();
    int httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (httpStatusCode)
        return ResourceError("HTTP"_s, httpStatusCode, URL(String(url.toString())), String(reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString()));

    return ResourceError("QtNetwork"_s, reply->error(), URL(String(url.toString())), String(reply->errorString()));
}

}

#include "moc_QNetworkReplyHandler.cpp"
