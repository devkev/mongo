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

#pragma once

#include <absl/container/flat_hash_map.h>

#include "mongo/base/status.h"
#include "mongo/transport/transport_layer.h"
#include "mongo/util/net/ssl_types.h"
#include "mongo/util/producer_consumer_queue.h"
#include "mongo/util/time_support.h"

namespace grpc {
    class Server;
}

namespace mongo {
namespace transport {

class TransportLayerGRPC : public TransportLayer {
    TransportLayerGRPC(const TransportLayerGRPC&) = delete;
    TransportLayerGRPC& operator=(const TransportLayerGRPC&) = delete;

public:
    constexpr static int kPortOffset = 1000;

    struct Options {
        constexpr static auto kIngress = 0x1;
        constexpr static auto kEgress = 0x10;

        explicit Options(const ServerGlobalParams* params);
        explicit Options(const std::vector<std::string>& ipList, int port);
        Options() = default;

        int mode = kIngress | kEgress;

        bool isIngress() const {
            return mode & kIngress;
        }

        bool isEgress() const {
            return mode & kEgress;
        }

        std::vector<std::string> ipList;               // addresses to bind to
        int port = ServerGlobalParams::DefaultDBPort;  // port to bind to
    };

    TransportLayerGRPC(const Options& options, ServiceEntryPoint* sep);
    ~TransportLayerGRPC();

    StatusWith<SessionHandle> connect(HostAndPort peer,
                                      ConnectSSLMode sslMode,
                                      Milliseconds timeout) override;
    Future<SessionHandle> asyncConnect(HostAndPort peer,
                                       ConnectSSLMode sslMode,
                                       const ReactorHandle& reactor,
                                       Milliseconds timeout) override;

    Status setup() override;
    Status start() override;
    void shutdown() override;

    virtual ReactorHandle getReactor(WhichReactor which) override;

private:
    class TransportServiceImpl;
    class GRPCSession;
    class GRPCEgressSession;

    using GRPCSessionHandle = std::shared_ptr<GRPCSession>;
    GRPCSessionHandle getLogicalSessionHandle(const std::string& lcid);

    Options _options;
    ServiceEntryPoint* const _sep = nullptr;
    stdx::thread _thread;
    std::unique_ptr<grpc::Server> _server;
    std::unique_ptr<TransportServiceImpl> _service;

    Mutex _mutex = MONGO_MAKE_LATCH("TransportLayerGRPC::_mutex");
    absl::flat_hash_map<std::string, GRPCSessionHandle> _sessions;
};

}  // namespace transport
}  // namespace mongo
