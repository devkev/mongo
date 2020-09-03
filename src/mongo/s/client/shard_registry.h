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

#include <string>
#include <vector>

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/client/connection_string.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/server_options.h"
#include "mongo/executor/task_executor.h"
#include "mongo/platform/mutex.h"
#include "mongo/s/client/shard.h"
#include "mongo/s/client/shard_factory.h"
#include "mongo/stdx/unordered_map.h"
#include "mongo/util/concurrency/thread_pool.h"
#include "mongo/util/net/hostandport.h"
#include "mongo/util/read_through_cache.h"

namespace mongo {

class ShardRegistryData {
public:
    using ShardMap = stdx::unordered_map<ShardId, std::shared_ptr<Shard>, ShardId::Hasher>;

    /**
     * Creates a basic ShardRegistryData, that only contains the config shard.  Needed during
     * initialization, when the config servers are contacted for the first time (ie. the first time
     * createFromCatalogClient() is called).
     */
    static ShardRegistryData createWithConfigShardOnly(std::shared_ptr<Shard> configShard);

    /**
     * Reads shards docs from the catalog client and fills in maps.
     */
    static std::pair<ShardRegistryData, Timestamp> createFromCatalogClient(
        OperationContext* opCtx, ShardFactory* shardFactory);

    /**
     * Merges alreadyCachedData and configServerData into a new ShardRegistryData.
     *
     * The merged data is the same as configServerData, except that for the host and connection
     * string based lookups, any values from alreadyCachedData will take precedence over those from
     * configServerData.
     *
     * Returns the merged data, as well as the shards that have been removed (ie. that are present
     * in alreadyCachedData but not configServerData) as a mapping from ShardId to
     * std::shared_ptr<Shard>.
     *
     * Called when reloading the shard registry. It is important to merge _hostLookup because
     * reloading the shard registry can interleave with updates to the shard registry passed by the
     * RSM.
     */
    static std::pair<ShardRegistryData, ShardMap> mergeExisting(
        const ShardRegistryData& alreadyCachedData, const ShardRegistryData& configServerData);

    /**
     * Create a duplicate of existingData, but additionally updates the shard for newConnString.
     * Used when notified by the RSM of a new connection string from a shard.
     */
    static std::pair<ShardRegistryData, std::shared_ptr<Shard>> createFromExisting(
        const ShardRegistryData& existingData,
        const ConnectionString& newConnString,
        ShardFactory* shardFactory);

    /**
     * Returns the shard with the given shard id, connection string, or host and port.
     *
     * Callers might pass in the connection string or HostAndPort rather than ShardId, so this
     * method will first look for the shard by ShardId, then connection string, then HostAndPort
     * stopping once it finds the shard.
     */
    std::shared_ptr<Shard> findShard(const ShardId& shardId) const;

    /**
     * Returns the shard with the given replica set name, or nullptr if no such shard.
     */
    std::shared_ptr<Shard> findByRSName(const std::string& name) const;

    /**
     * Returns the shard which contains a mongod with the given host and port, or nullptr if no such
     * shard.
     */
    std::shared_ptr<Shard> findByHostAndPort(const HostAndPort&) const;

    /**
     * Returns the set of all known shard ids.
     */
    void getAllShardIds(std::set<ShardId>& result) const;

    /**
     * Returns the set of all known shard objects.
     */
    void getAllShards(std::vector<std::shared_ptr<Shard>>& result) const;

    void toBSON(BSONObjBuilder* result) const;
    void toBSON(BSONObjBuilder* map, BSONObjBuilder* hosts, BSONObjBuilder* connStrings) const;
    BSONObj toBSON() const;

private:
    /**
     * Returns the shard with the given shard id, or nullptr if no such shard.
     */
    std::shared_ptr<Shard> _findByShardId(const ShardId&) const;

    /**
     * Returns the shard with the given connection string, or nullptr if no such shard.
     */
    std::shared_ptr<Shard> _findByConnectionString(const ConnectionString& connectionString) const;

    /**
     * Puts the given shard object into the lookup maps.
     *
     * If useOriginalCS = true it will use the ConnectionSring used for shard creation to update
     * lookup maps. Otherwise the current connection string from the Shard's RemoteCommandTargeter
     * will be used. Only called during ShardRegistryData construction.
     */
    void _addShard(std::shared_ptr<Shard>, bool useOriginalCS);

    // Map of shardName -> Shard
    ShardMap _shardIdLookup;

    // Map from replica set name to shard corresponding to this replica set
    ShardMap _rsLookup;

    // Map of HostAndPort to Shard
    stdx::unordered_map<HostAndPort, std::shared_ptr<Shard>> _hostLookup;

    // Map of connection string to Shard
    std::map<ConnectionString, std::shared_ptr<Shard>> _connStringLookup;
};

/**
 * Maintains the set of all shards known to the instance and their connections and exposes
 * functionality to run commands against shards. All commands which this registry executes are
 * retried on NotMaster class of errors and in addition all read commands are retried on network
 * errors automatically as well.
 */
class ShardRegistry {
    ShardRegistry(const ShardRegistry&) = delete;
    ShardRegistry& operator=(const ShardRegistry&) = delete;

public:
    /**
     * A ShardId for the config servers.
     */
    static const ShardId kConfigServerShardId;

    /**
     * A callback type for functions that can be called on shard removal.
     */
    using ShardRemovalHook = std::function<void(const ShardId&)>;

    /**
     * Instantiates a new shard registry.
     *
     * @param shardFactory      Makes shards
     * @param configServerCS    ConnectionString used for communicating with the config servers
     * @param shardRemovalHooks A list of hooks that will be called when a shard is removed. The
     *                          hook is expected not to throw. If it does throw, the process will be
     *                          terminated.
     */
    ShardRegistry(std::unique_ptr<ShardFactory> shardFactory,
                  const ConnectionString& configServerCS,
                  std::vector<ShardRemovalHook> shardRemovalHooks = {});

    ~ShardRegistry();

    /**
     * Initializes ShardRegistry with config shard. Must be called outside c-tor to avoid calls on
     * this while its still not fully constructed.
     */
    void init(ServiceContext* service);

    /**
     * Startup the periodic reloader of the ShardRegistry.
     * Can be called only after ShardRegistry::init()
     */
    void startupPeriodicReloader(OperationContext* opCtx);

    /**
     * Shutdown the periodic reloader of the ShardRegistry.
     */
    void shutdownPeriodicReloader();

    /**
     * Shuts down the threadPool. Needs to be called explicitly because ShardRegistry is never
     * destroyed as it's owned by the static grid object.
     */
    void shutdown();

    /**
     * This is invalid to use on the config server and will hit an invariant if it is done.
     * If the config server has need of a connection string for itself, it should get it from the
     * replication state.
     *
     * Returns the connection string for the config server.
     */
    ConnectionString getConfigServerConnectionString() const;

    /**
     * Returns shared pointer to the shard object representing the config servers.
     */
    std::shared_ptr<Shard> getConfigShard() const;

    /**
     * Returns a shared pointer to the shard object with the given shard id, or ShardNotFound error
     * otherwise.
     *
     * May refresh the shard registry if there's no cached information about the shard. The shardId
     * parameter can actually be the shard name or the HostAndPort for any server in the shard.
     */
    StatusWith<std::shared_ptr<Shard>> getShard(OperationContext* opCtx, const ShardId& shardId);

    /**
     * Populates all known shard ids into the given vector.
     */
    void getAllShardIds(OperationContext* opCtx, std::vector<ShardId>* all);

    /**
     * Returns the number of shards.
     */
    int getNumShards(OperationContext* opCtx);

    /**
     * Takes a connection string describing either a shard or config server replica set, looks
     * up the corresponding Shard object based on the replica set name, then updates the
     * ShardRegistry's notion of what hosts make up that shard.
     */
    void updateReplSetHosts(const ConnectionString& newConnString);

    /**
     * Instantiates a new detached shard connection, which does not appear in the list of shards
     * tracked by the registry and as a result will not be returned by getAllShardIds.
     *
     * The caller owns the returned shard object and is responsible for disposing of it when done.
     *
     * @param connStr Connection string to the shard.
     */
    std::unique_ptr<Shard> createConnection(const ConnectionString& connStr) const;

    /**
     * The ShardRegistry is "up" once a successful lookup from the config servers has been
     * completed.
     */
    bool isUp() const;

    void toBSON(OperationContext* opCtx, BSONObjBuilder* result) const;

    /**
     * Reloads the ShardRegistry based on the contents of the config server's config.shards
     * collection. Returns true if this call performed a reload and false if this call only waited
     * for another thread to perform the reload and did not actually reload. Because of this, it is
     * possible that calling reload once may not result in the most up to date view. If strict
     * reloading is required, the caller should call this method one more time if the first call
     * returned false.
     */
    bool reload(OperationContext* opCtx);

    /**
     * Clears all entries from the shard registry entries, which will force the registry to do a
     * reload on next access.
     */
    void clearEntries(OperationContext* opCtx);

    /**
     * For use in mongos which needs notifications about changes to shard replset membership to
     * update the config.shards collection.
     */
    static void updateReplicaSetOnConfigServer(ServiceContext* serviceContex,
                                               const ConnectionString& connStr) noexcept;

    // TODO SERVER-50206: Remove usage of these non-causally consistent accessors one by one.

    /**
     * Returns a shared pointer to the shard object with the given shard id. The shardId parameter
     * can actually be the shard name or the HostAndPort for any server in the shard. Will not
     * refresh the shard registry or otherwise perform any network traffic. This means that if the
     * shard was recently added it may not be found.  USE WITH CAUTION.
     */
    std::shared_ptr<Shard> getShardNoReload(const ShardId& shardId) const;

    /**
     * Finds the Shard that the mongod listening at this HostAndPort is a member of. Will not
     * refresh the shard registry or otherwise perform any network traffic.
     */
    std::shared_ptr<Shard> getShardForHostNoReload(const HostAndPort& shardHost) const;

    void getAllShardIdsNoReload(std::vector<ShardId>* all) const;

    int getNumShardsNoReload() const;

private:
    /**
     * The ShardRegistry uses the ReadThroughCache to handle refreshing itself.  The cache stores
     * a single entry, with key of Singleton, value of ShardRegistryData, and causal-consistency
     * time which is primarily Timestamp (based on the TopologyTime), but with additional
     * "increment"s that are used to flag additional refresh criteria.
     */

    using Increment = int64_t;

    struct Time {
        bool operator==(const Time& other) const {
            return topologyTime == other.topologyTime && rsmIncrement == other.rsmIncrement &&
                forceReloadIncrement == other.forceReloadIncrement;
        }
        bool operator!=(const Time& other) const {
            return !(*this == other);
        }
        bool operator>(const Time& other) const {
            return topologyTime > other.topologyTime || rsmIncrement > other.rsmIncrement ||
                forceReloadIncrement > other.forceReloadIncrement;
        }
        bool operator>=(const Time& other) const {
            return (*this > other) || (*this == other);
        }
        bool operator<(const Time& other) const {
            return !(*this >= other);
        }
        bool operator<=(const Time& other) const {
            return !(*this > other);
        }

        BSONObj toBSON() const {
            BSONObjBuilder bob;
            bob.append("topologyTime", topologyTime);
            bob.append("rsmIncrement", rsmIncrement);
            bob.append("forceReloadIncrement", forceReloadIncrement);
            return bob.obj();
        }

        Timestamp topologyTime;

        // The increments are used locally to trigger the lookup function.  The rsmIncrement is
        // used to indicate that that there are stashed RSM updates that need to be incorporated,
        // and the forceReloadIncrement is used to indicate that the latest data should be fetched
        // from the configsvrs (ie. when the topologyTime can't be used for this, eg. in the first
        // lookup, and in contexts like unittests where topologyTime isn't gossipped but the
        // ShardRegistry still needs to be reloaded.
        Increment rsmIncrement{0};
        Increment forceReloadIncrement{0};
    };

    enum class Singleton { Only };
    static constexpr auto _kSingleton = Singleton::Only;

    using Cache = ReadThroughCache<Singleton, ShardRegistryData, Time>;

    /**
     * Gets a causally-consistent (ie. latest-known) copy of the ShardRegistryData, refreshing from
     * the config servers if necessary.
     */
    Cache::ValueHandle _getData(OperationContext* opCtx);

    /**
     * Gets the latest-cached copy of the ShardRegistryData.  Never fetches from the config servers.
     * Only used by the "NoReload" accessors.
     * TODO SERVER-50206: Remove usage of this non-causally consistent accessor.
     */
    Cache::ValueHandle _getCachedData() const;

    /**
     * Lookup shard by replica set name. Returns nullptr if the name can't be found.
     * Note: this doesn't refresh the table if the name isn't found, so it's possible that a
     * newly added shard/Replica Set may not be found.
     * TODO SERVER-50206: Remove usage of this non-causally consistent accessor.
     */
    std::shared_ptr<Shard> _getShardForRSNameNoReload(const std::string& name) const;

    using LatestConnStrings = stdx::unordered_map<ShardId, ConnectionString, ShardId::Hasher>;

    std::vector<LatestConnStrings::value_type> _getLatestConnStrings() const;


    void _periodicReload(const executor::TaskExecutor::CallbackArgs& cbArgs);

    /**
     * Factory to create shards.  Never changed after startup so safe to access outside of _mutex.
     */
    const std::unique_ptr<ShardFactory> _shardFactory;

    /**
     * Specified in the ShardRegistry c-tor. It's used only in init() to initialize the config
     * shard.
     */
    const ConnectionString _initConfigServerCS;

    // Executor for periodic reloading.
    std::unique_ptr<executor::TaskExecutor> _executor{};

    /**
     * A list of callbacks to be called asynchronously when it has been discovered that a shard was
     * removed.
     */
    const std::vector<ShardRemovalHook> _shardRemovalHooks;

    mutable Mutex _cacheMutex = MONGO_MAKE_LATCH("ShardRegistry::_cacheMutex");
    std::unique_ptr<Cache> _cache;

    ThreadPool _threadPool;
    AtomicWord<Increment> _rsmIncrement{0};
    AtomicWord<Increment> _forceReloadIncrement{0};

    // Protects _configShardData and _latestNewConnStrings.
    mutable Mutex _mutex = MONGO_MAKE_LATCH("ShardRegistry::_mutex");

    // Store a reference to the configShard.
    ShardRegistryData _configShardData;

    // The key is replset name (the type is ShardId just to take advantage of its hasher).
    LatestConnStrings _latestConnStrings;

    AtomicWord<bool> _isInitialized{false};

    // The ShardRegistry is "up" once there has been a successful refresh.
    AtomicWord<bool> _isUp{false};

    // Set to true in shutdown call to prevent calling it twice.
    AtomicWord<bool> _isShutdown{false};

    ServiceContext* _service{nullptr};
};

}  // namespace mongo
