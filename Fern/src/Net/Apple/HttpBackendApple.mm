// The HTTP backend of Fern::Net on macOS and iOS: NSURLSession with an ephemeral configuration.
// This file is compiled with ARC (Fern/CMakeLists.txt): the session, its tasks and the delegate
// are managed by the compiler, the rest of Fern stays as it is.

#import "Net/HttpInternal.hpp"

#import <Foundation/Foundation.h>

#include <condition_variable>
#include <mutex>
#include <unordered_map>

namespace Fern::Net::Detail {
class AppleBackend;
}

@interface FernNetSessionDelegate : NSObject <NSURLSessionDataDelegate>
@property(nonatomic, assign) Fern::Net::Detail::AppleBackend *backend;
@end

namespace Fern::Net::Detail {

namespace {

Error mapError(NSError *error) {
    if (!error) { return Error::None; }
    if ([error.domain isEqualToString:NSURLErrorDomain]) {
        switch (error.code) {
            case NSURLErrorBadURL:
            case NSURLErrorUnsupportedURL: return Error::InvalidUrl;
            case NSURLErrorTimedOut: return Error::Timeout;
            case NSURLErrorCannotFindHost:
            case NSURLErrorDNSLookupFailed: return Error::HostNotFound;
            case NSURLErrorNotConnectedToInternet:
            case NSURLErrorInternationalRoamingOff:
            case NSURLErrorDataNotAllowed: return Error::Offline;
            case NSURLErrorSecureConnectionFailed:
            case NSURLErrorServerCertificateHasBadDate:
            case NSURLErrorServerCertificateUntrusted:
            case NSURLErrorServerCertificateHasUnknownRoot:
            case NSURLErrorServerCertificateNotYetValid:
            case NSURLErrorClientCertificateRejected:
            case NSURLErrorClientCertificateRequired:
            case NSURLErrorAppTransportSecurityRequiresSecureConnection: return Error::TlsFailed;
            case NSURLErrorHTTPTooManyRedirects:
            case NSURLErrorBadServerResponse:
            case NSURLErrorZeroByteResource:
            case NSURLErrorCannotDecodeRawData:
            case NSURLErrorCannotDecodeContentData:
            case NSURLErrorCannotParseResponse: return Error::Protocol;
            case NSURLErrorCannotWriteToFile:
            case NSURLErrorCannotOpenFile:
            case NSURLErrorCannotCreateFile:
            case NSURLErrorFileDoesNotExist:
            case NSURLErrorNoPermissionsToReadFile: return Error::FileError;
            default: return Error::ConnectionFailed;
        }
    }
    return Error::ConnectionFailed;
}

std::vector<Header> headersOf(NSHTTPURLResponse *response) {
    std::vector<Header> headers;
    std::vector<Header> *out = &headers; // a block captures by value; the pointer stays writable
    [response.allHeaderFields enumerateKeysAndObjectsUsingBlock:^(id key, id value, BOOL *) {
        if ([key isKindOfClass:[NSString class]] && [value isKindOfClass:[NSString class]]) {
            out->push_back({std::string([key UTF8String]), std::string([value UTF8String])});
        }
    }];
    return headers;
}

} // namespace

class AppleBackend final : public Backend {
public:
    explicit AppleBackend(const ClientDesc &desc) {
        m_delegate = [FernNetSessionDelegate new];
        m_delegate.backend = this;
        NSOperationQueue *queue = [NSOperationQueue new];
        queue.maxConcurrentOperationCount = 1;
        queue.name = @"Fern Net";

        NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        configuration.HTTPCookieAcceptPolicy = NSHTTPCookieAcceptPolicyNever;
        configuration.HTTPShouldSetCookies = NO;
        configuration.URLCache = nil;
        configuration.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
        configuration.HTTPMaximumConnectionsPerHost = desc.maxConnectionsPerHost;
        configuration.HTTPAdditionalHeaders = @{@"User-Agent": @(desc.userAgent.c_str())};
        configuration.timeoutIntervalForRequest = 30;
        configuration.timeoutIntervalForResource = 7 * 24 * 3600;
        m_session = [NSURLSession sessionWithConfiguration:configuration delegate:m_delegate delegateQueue:queue];
    }

    void start(const std::shared_ptr<Transfer> &transfer) override {
        const Request &request = transfer->request();
        NSURL *url = [NSURL URLWithString:@(request.url.c_str())];
        if (!url) {
            transfer->finish(Error::InvalidUrl);
            return;
        }
        NSMutableURLRequest *urlRequest =
            [[NSMutableURLRequest alloc] initWithURL:url
                                         cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                     timeoutInterval:request.timeouts.idleSeconds > 0 ? request.timeouts.idleSeconds : 30];
        urlRequest.HTTPMethod = @(methodName(request.method));
        urlRequest.HTTPShouldHandleCookies = NO;
        for (const Header &header : transfer->requestHeaders()) {
            [urlRequest setValue:@(header.value.c_str()) forHTTPHeaderField:@(header.name.c_str())];
        }
        if (!request.body.empty()) {
            urlRequest.HTTPBody = [NSData dataWithBytes:request.body.data() length:request.body.size()];
        }

        NSURLSessionTask *task;
        if (!request.uploadFile.empty()) {
            NSURL *fileUrl = [NSURL fileURLWithPath:@(request.uploadFile.c_str())];
            task = [m_session uploadTaskWithRequest:urlRequest fromFile:fileUrl];
        } else {
            task = [m_session dataTaskWithRequest:urlRequest];
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tasks[task.taskIdentifier] = TaskState{transfer, task, 0};
            m_byTransfer[transfer.get()] = task.taskIdentifier;
        }
        [task resume];
    }

    void abort(Transfer &transfer) override {
        NSURLSessionTask *task = nil;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_byTransfer.find(&transfer);
            if (it != m_byTransfer.end()) { task = m_tasks[it->second].task; }
        }
        [task cancel];
    }

    void shutdown() override {
        [m_session invalidateAndCancel];
        std::unique_lock<std::mutex> lock(m_mutex);
        m_invalidCv.wait_for(lock, std::chrono::seconds(5), [this] { return m_invalidated; });
        m_session = nil;
        m_delegate = nil;
    }

    // ---- The delegate, on its serial queue ----

    void redirect(NSURLSessionTask *task, NSURLRequest *request, void (^handler)(NSURLRequest *)) {
        std::shared_ptr<Transfer> transfer;
        int redirects = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_tasks.find(task.taskIdentifier);
            if (it == m_tasks.end()) {
                handler(nil);
                return;
            }
            transfer = it->second.transfer;
            redirects = ++it->second.redirects;
        }
        if (redirects > 10) {
            transfer->recordError(Error::Protocol);
            handler(nil);
            return;
        }
        Error error = redirectError(transfer->url(), request.URL.absoluteString.UTF8String, transfer->request().allowInsecure);
        if (error != Error::None) {
            transfer->recordError(error);
            handler(nil);
            return;
        }
        handler(request);
    }

    NSURLSessionResponseDisposition response(NSURLSessionTask *task, NSURLResponse *response) {
        std::shared_ptr<Transfer> transfer = find(task);
        if (!transfer) { return NSURLSessionResponseCancel; }
        NSHTTPURLResponse *http = [response isKindOfClass:[NSHTTPURLResponse class]] ? (NSHTTPURLResponse *)response : nil;
        if (!http) {
            transfer->recordError(Error::Protocol);
            return NSURLSessionResponseCancel;
        }
        std::string finalUrl = response.URL ? std::string(response.URL.absoluteString.UTF8String) : std::string();
        if (!transfer->onResponse(int(http.statusCode), headersOf(http), std::move(finalUrl))) {
            return NSURLSessionResponseCancel;
        }
        return NSURLSessionResponseAllow;
    }

    void data(NSURLSessionTask *task, NSData *data) {
        std::shared_ptr<Transfer> transfer = find(task);
        if (!transfer) { return; }
        __block bool keepGoing = true;
        Transfer *raw = transfer.get();
        [data enumerateByteRangesUsingBlock:^(const void *bytes, NSRange range, BOOL *stop) {
            if (!raw->onData(static_cast<const uint8_t *>(bytes), range.length)) {
                keepGoing = false;
                *stop = YES;
            }
        }];
        if (!keepGoing) { [task cancel]; }
    }

    void completed(NSURLSessionTask *task, NSError *error) {
        std::shared_ptr<Transfer> transfer;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_tasks.find(task.taskIdentifier);
            if (it == m_tasks.end()) { return; }
            transfer = it->second.transfer;
            m_byTransfer.erase(transfer.get());
            m_tasks.erase(it);
        }
        // A cancel of ours arrives as NSURLErrorCancelled: the recorded error (or the cancellation
        // of the transfer) says what happened.
        Error transportError = mapError(error);
        if (error && [error.domain isEqualToString:NSURLErrorDomain] && error.code == NSURLErrorCancelled) {
            transportError = Error::ConnectionFailed;
        }
        transfer->finish(transportError);
    }

    void invalidated() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_invalidated = true;
        m_invalidCv.notify_all();
    }

private:
    struct TaskState {
        std::shared_ptr<Transfer> transfer;
        NSURLSessionTask *task;
        int redirects;
    };

    std::shared_ptr<Transfer> find(NSURLSessionTask *task) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_tasks.find(task.taskIdentifier);
        return it == m_tasks.end() ? nullptr : it->second.transfer;
    }

    NSURLSession *m_session = nil;
    FernNetSessionDelegate *m_delegate = nil;
    std::mutex m_mutex;
    std::condition_variable m_invalidCv;
    bool m_invalidated = false;
    std::unordered_map<NSUInteger, TaskState> m_tasks;
    std::unordered_map<Transfer *, NSUInteger> m_byTransfer;
};

std::unique_ptr<Backend> Backend::create(const ClientDesc &desc) {
    return std::make_unique<AppleBackend>(desc);
}

} // namespace Fern::Net::Detail

@implementation FernNetSessionDelegate

- (void)URLSession:(NSURLSession *)session
                          task:(NSURLSessionTask *)task
    willPerformHTTPRedirection:(NSHTTPURLResponse *)response
                    newRequest:(NSURLRequest *)request
             completionHandler:(void (^)(NSURLRequest *))completionHandler {
    self.backend->redirect(task, request, completionHandler);
}

- (void)URLSession:(NSURLSession *)session
              dataTask:(NSURLSessionDataTask *)dataTask
    didReceiveResponse:(NSURLResponse *)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler {
    completionHandler(self.backend->response(dataTask, response));
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)dataTask didReceiveData:(NSData *)data {
    self.backend->data(dataTask, data);
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error {
    self.backend->completed(task, error);
}

- (void)URLSession:(NSURLSession *)session didBecomeInvalidWithError:(NSError *)error {
    self.backend->invalidated();
}

@end
