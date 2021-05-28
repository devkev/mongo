/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#define GRPC_CALLBACK_API_NONEXPERIMENTAL 1
#include <grpcpp/grpcpp.h>

#include "mongo/transport/mongodb.grpc.pb.h"
#include "mongo/transport/mongodb.pb.h"

#include "mongo/transport/transport_layer_grpc.h"
#include "mongo/transport/reactor_asio.h"
#include "mongo/transport/service_entry_point.h"
#include "mongo/transport/transport_layer_asio.h"

#include "mongo/platform/basic.h"

#include "mongo/db/stats/counters.h"

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <fmt/format.h>

#include <memory>

namespace mongo {
namespace transport {

// borrowed from https://jguegant.github.io/blogs/tech/performing-try-emplace.html
template <class Factory>
struct lazy_convert_construct {
    using result_type = std::invoke_result_t<const Factory&>;

    constexpr lazy_convert_construct(Factory&& factory) : factory_(std::move(factory)) {}
    constexpr operator result_type() const noexcept(std::is_nothrow_invocable_v<const Factory&>) {
        return factory_();
    }

    Factory factory_;
};

class TransportLayerGRPC::TransportServiceImpl final : public mongodb::Transport::CallbackService {
public:
    explicit TransportServiceImpl(TransportLayerGRPC* transportLayer);

private:
    grpc::ServerUnaryReactor* SendMessage(grpc::CallbackServerContext* context,
                                            const mongodb::Message* request,
                                            mongodb::Message* response) override;

    TransportLayerGRPC* _tl;
};

class TransportLayerGRPC::GRPCSession : public Session {
    GRPCSession(const GRPCSession&) = delete;
    GRPCSession& operator=(const GRPCSession&) = delete;

public:
    explicit GRPCSession(TransportLayer* tl, const std::string& lcid)
        : _tl(checked_cast<TransportLayerGRPC*>(tl)), _lcid(lcid) {
        setTags(kDefaultBatonHack);
    }

    ~GRPCSession() {
        end();
    }

    TransportLayer* getTransportLayer() const override {
        return _tl;
    }

    const std::string& lcid() const {
        return _lcid;
    }

    const HostAndPort& remote() const override {
        return _remote;
    }

    const HostAndPort& local() const override {
        return _local;
    }

    const SockAddr& remoteAddr() const override {
        return _remoteAddr;
    }

    const SockAddr& localAddr() const override {
        return _localAddr;
    }

    void end() override;
    StatusWith<Message> sourceMessage() override;
    Future<Message> asyncSourceMessage(const BatonHandle& handle = nullptr) override {
        return Future<Message>::makeReady(sourceMessage());
    }

    Status sinkMessage(Message message) override;
    Future<void> asyncSinkMessage(Message message,
                                    const BatonHandle& handle = nullptr) override {
        return Future<void>::makeReady(sinkMessage(message));
    }

    // TODO: do we need these?
    void cancelAsyncOperations(const BatonHandle& handle = nullptr) override {}
    void setTimeout(boost::optional<Milliseconds>) override {}
    bool isConnected() override {
        return true;
    }

protected:
    friend class TransportLayerGRPC::TransportServiceImpl;
    TransportLayerGRPC* _tl;
    std::string _lcid;

    HostAndPort _remote{};
    HostAndPort _local{};
    SockAddr _remoteAddr;
    SockAddr _localAddr;

    struct PendingRequest {
        const mongodb::Message* request;
        mongodb::Message* response;
        grpc::ServerUnaryReactor* reactor;
    };

    Mutex _mutex = MONGO_MAKE_LATCH("GRPCSession::_mutex");
    // TODO: using PendingRequestPtr = std::unique_ptr<PendingRequest>;
    MultiProducerSingleConsumerQueue<PendingRequest*> _pendingRequests;
    PendingRequest* _currentRequest = nullptr;
};

class TransportLayerGRPC::GRPCEgressSession : public Session {
    GRPCEgressSession(const GRPCEgressSession&) = delete;
    GRPCEgressSession& operator=(const GRPCEgressSession&) = delete;

public:
    explicit GRPCEgressSession(TransportLayer* tl, std::shared_ptr<grpc::Channel> channel)
        : _tl(checked_cast<TransportLayerGRPC*>(tl)),
            _lcid(UUID::gen().toString()),
            _stub(mongodb::Transport::NewStub(channel)) {
        setTags(kDefaultBatonHack);
    }

    ~GRPCEgressSession() {
        end();
    }

    TransportLayer* getTransportLayer() const override {
        return _tl;
    }

    const std::string& lcid() const {
        return _lcid;
    }

    const HostAndPort& remote() const override {
        return _remote;
    }

    const HostAndPort& local() const override {
        return _local;
    }

    const SockAddr& remoteAddr() const override {
        return _remoteAddr;
    }

    const SockAddr& localAddr() const override {
        return _localAddr;
    }

    void end() override;
    StatusWith<Message> sourceMessage() override;
    Future<Message> asyncSourceMessage(const BatonHandle& handle = nullptr) override {
        return Future<Message>::makeReady(sourceMessage());
    }

    Status sinkMessage(Message message) override {
        return asyncSinkMessage(message).getNoThrow();
    }
    Future<void> asyncSinkMessage(Message message,
                                    const BatonHandle& handle = nullptr) override;

    void cancelAsyncOperations(const BatonHandle& handle = nullptr) override {}
    void setTimeout(boost::optional<Milliseconds>) override {}
    bool isConnected() override {
        return true;
    }

protected:
    TransportLayerGRPC* _tl;
    std::string _lcid;

    HostAndPort _remote{};
    HostAndPort _local{};
    SockAddr _remoteAddr;
    SockAddr _localAddr;

    std::unique_ptr<mongodb::Transport::Stub> _stub;
    SingleProducerSingleConsumerQueue<Message> _responses;

    struct PendingRequest {
        std::unique_ptr<mongodb::Message> request;
        std::unique_ptr<mongodb::Message> response;
        std::unique_ptr<grpc::ClientContext> context;
        Promise<void> promise;
    };

    Mutex _mutex = MONGO_MAKE_LATCH("GRPCEgressSession::_mutex");
    std::list<std::weak_ptr<PendingRequest>> _pendingRequests;
};

Message messageFromPayload(const std::string& payload) {
    auto requestBuffer = SharedBuffer::allocate(payload.size());
    memcpy(requestBuffer.get(), payload.data(), payload.size());
    return Message(std::move(requestBuffer));
}

TransportLayerGRPC::TransportServiceImpl::TransportServiceImpl(TransportLayerGRPC* tl) : _tl(tl) {}
grpc::ServerUnaryReactor* TransportLayerGRPC::TransportServiceImpl::SendMessage(
    grpc::CallbackServerContext* context,
    const mongodb::Message* request,
    mongodb::Message* response) {
    grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
    const auto clientMetadata = context->client_metadata();
    auto it = clientMetadata.find("lcid");
    if (it == clientMetadata.end()) {
        reactor->Finish(
            grpc::Status(grpc::INVALID_ARGUMENT, "missing required logical connection id"));
        return reactor;
    }

    auto lcid = std::string(it->second.begin(), it->second.end());
    auto session = _tl->getLogicalSessionHandle(lcid);
    session->_pendingRequests.push(new GRPCSession::PendingRequest{request, response, reactor});
    return reactor;
}

void TransportLayerGRPC::GRPCSession::end() {
    auto [requests, bytes] = _pendingRequests.popMany();
    for (auto request : requests) {
        request->reactor->Finish(grpc::Status::CANCELLED);
    }
    requests.clear();
}

StatusWith<Message> TransportLayerGRPC::GRPCSession::sourceMessage() {
    _currentRequest = _pendingRequests.pop();
    auto requestMessage = messageFromPayload(_currentRequest->request->payload());
    networkCounter.hitPhysicalIn(requestMessage.size());
    return std::move(requestMessage);
}

Status TransportLayerGRPC::GRPCSession::sinkMessage(Message message) {
    networkCounter.hitPhysicalOut(message.size());
    _currentRequest->response->set_payload(std::string(message.buf(), message.size()));
    _currentRequest->reactor->Finish(grpc::Status::OK);

    delete _currentRequest;
    _currentRequest = nullptr;

    return Status::OK();
}

void TransportLayerGRPC::GRPCEgressSession::end() {
    stdx::lock_guard<Latch> lk(_mutex);
    for (auto pendingRequest : _pendingRequests) {
        if (auto pr = pendingRequest.lock()) {
            pr->context->TryCancel();
        }
    }
    _pendingRequests.clear();
}

StatusWith<Message> TransportLayerGRPC::GRPCEgressSession::sourceMessage() {
    return _responses.pop();
}

Future<void> TransportLayerGRPC::GRPCEgressSession::asyncSinkMessage(Message message,
                                                                     const BatonHandle& handle) {
    auto pr = std::make_shared<PendingRequest>();
    pr->response = std::make_unique<mongodb::Message>();

    pr->request = std::make_unique<mongodb::Message>();
    pr->request->set_payload(std::string(message.buf(), message.size()));
    networkCounter.hitPhysicalOut(message.size());
    networkCounter.hitLogicalOut(message.size());

    pr->context = std::make_unique<grpc::ClientContext>();
    pr->context->AddMetadata("lcid", _lcid);

    // TODO: Cancellation might be a little inefficiently implemented here
    std::list<std::weak_ptr<PendingRequest>>::iterator it;
    {
        stdx::lock_guard<Latch> lk(_mutex);
        it = _pendingRequests.emplace(_pendingRequests.end(), pr);
    }

    auto pf = makePromiseFuture<void>();
    pr->promise = std::move(pf.promise);

    _stub->async()->SendMessage(pr->context.get(),
                                pr->request.get(),
                                pr->response.get(),
                                [this, pr, it = std::move(it)](grpc::Status s) {
                                    // remove the pending request context
                                    {
                                        stdx::lock_guard<Latch> lk(_mutex);
                                        _pendingRequests.erase(it);
                                    }

                                    if (!s.ok()) {
                                        pr->promise.setError({ErrorCodes::InternalError, s.error_message()});
                                        return;
                                    }

                                    auto message = messageFromPayload(pr->response->payload());
                                    networkCounter.hitPhysicalIn(message.size());
                                    networkCounter.hitLogicalIn(message.size());
                                    _responses.push(std::move(message));
                                    pr->promise.emplaceValue();
                                });

    return std::move(pf.future);
}

TransportLayerGRPC::Options::Options(const ServerGlobalParams* params)
    : ipList(params->bind_ips), port(params->port) {}

TransportLayerGRPC::Options::Options(const std::vector<std::string>& ipList, int port)
    : ipList(ipList), port(port) {}

TransportLayerGRPC::TransportLayerGRPC(const Options& options, ServiceEntryPoint* sep)
    : _options(options), _sep(sep), _service(std::make_unique<TransportServiceImpl>(this)) {}

TransportLayerGRPC::~TransportLayerGRPC() {
    shutdown();
}

StatusWith<SessionHandle> TransportLayerGRPC::connect(HostAndPort peer,
                                                      ConnectSSLMode sslMode,
                                                      Milliseconds timeout) {
    std::cout << fmt::format("creating new egress connection to: {}\n", peer.toString());
    SessionHandle session(new GRPCEgressSession(
        this, grpc::CreateChannel(peer.toString(), grpc::InsecureChannelCredentials())));
    return std::move(session);
}

Future<SessionHandle> TransportLayerGRPC::asyncConnect(HostAndPort peer,
                                                       ConnectSSLMode sslMode,
                                                       const ReactorHandle& reactor,
                                                       Milliseconds timeout) {
    return Future<SessionHandle>::makeReady(connect(peer, sslMode, timeout));
}

Status TransportLayerGRPC::setup() {
    return Status::OK();
}

Status TransportLayerGRPC::start() {
    if (_options.isIngress()) {
        _thread = stdx::thread([this] {
            setThreadName("grpcListener");

            grpc::EnableDefaultHealthCheckService(true);
            grpc::reflection::InitProtoReflectionServerBuilderPlugin();

            grpc::ServerBuilder builder;
            if (_options.ipList.size()) {
                for (auto ip : _options.ipList) {
                    auto address = fmt::format("{}:{}", ip, _options.port);
                    std::cout << "listening on : " << address << std::endl;
                    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
                }
            } else {
                std::cout << "listening to everything on default port" << std::endl;
                builder.AddListeningPort("0.0.0.0:27017", grpc::InsecureServerCredentials());
            }

            builder.RegisterService(_service.get());
            _server = builder.BuildAndStart();
            _server->Wait();
        });
    }

    return Status::OK();
}

void TransportLayerGRPC::shutdown() {
    _sessions.clear();
    _server->Shutdown();
}

thread_local ASIOReactor* ASIOReactor::_reactorForThread = nullptr;
ReactorHandle TransportLayerGRPC::getReactor(WhichReactor which) {
    invariant(which == TransportLayer::kNewReactor);
    return std::make_shared<ASIOReactor>();
}

TransportLayerGRPC::GRPCSessionHandle TransportLayerGRPC::getLogicalSessionHandle(
    const std::string& lcid) {
    // NOTE: maybe less lock contention with this approach, but requires double lookup for
    //       session id.
    // absl::flat_hash_map<std::string, GRPCSessionHandle>::iterator sit = _sessions.find(lcid);
    // if (sit == _sessions.end()) {
    //     stdx::lock_guard<Latch> lk(_mutex);
    //     auto [entry, emplaced] =
    //         _sessions.emplace(lcid, std::make_shared<GRPCSession>(this, lcid));
    //     if (!emplaced) {
    //         // throw an exception?
    //     }

    //     _sep->startSession(entry->second);
    //     return entry->second;
    // }
    // return sit->second;

    std::pair<absl::flat_hash_map<std::string, GRPCSessionHandle>::iterator, bool> result;
    stdx::lock_guard<Latch> lk(_mutex);
    result = _sessions.try_emplace(
        lcid, lazy_convert_construct([&] { return std::make_shared<GRPCSession>(this, lcid); }));

    if (result.second) {
        // TODO: how are these ever removed!
        _sep->startSession(result.first->second);
    }

    return result.first->second;
}

}  // namespace transport
}  // namespace mongo
