Naming
-------

The feature:

- Filtered Replication
- Subset Replication
- Partial Replication

The replica set members:

- Filtered Member
- Subset member
- Partial member

- Oplog Only Member
- Oplog Member
- Minimal Member
- Minimum Member


Summary
--------

Filtered replication allows some secondaries to hold and replicate only a subset of the data held and replicated by regular replica set members.


Replset Configuration
----------------------

- replset config has filtering rules for member
- include and exclude patterns
    - strings or Reg ex
    - db level only? or collection also?
    - in single string? regex? glob-styl pattern? Or sub doc with db and coll fields ala auth?
- default is to include all namespaces


Semantics/Behaviour
--------------------

- oplog is always whole, i.e. includes entries for all namespaces, including those filtered out

- simply no op repl ops which are on filtered namespaces, instead of applying them
    - later improvement is to not add such ops to the list of ops that are being applied, so as to not even engage a parallel repl writer thread for the op
    - improves perf, esp in the case where most ops are filtered (eg. oplog-only nodes)

- WRITES DO NOT COUNT FOR WRITE CONCERN AT ALL (EVEN IF THE WRITE WAS APPLIED)
    - this might negatively affect w:majority - be careful
    - later maybe as a separate project this could be relaxed and count for write concern if the op has actually been applied (i.e. if the filters match the op in question). BUT this MUST also relax the initial sync restriction - if the op being applied on the filtered node is to count for write concern, then it must be possible to initial sync a COMPLETE unfiltered node from several filtered nodes.

- extra rule for initial sync source, cannot initial sync from node with filtered namespaces
    - could be relaxed later to allow initial sync from nodes which have a super set of my namespaces
    - could be relaxed further to allow initial syncing from SEVERAL filtered nodes which together have all the namespaces
    - ideally, only avoid as sync source for initial sync.  as a sync source for normal repl (chaining) should be fine, since the oplog is whole)

- initial sync simply skips filtered namespaces when cloning
    - oplog catchup has normal filtered repl op rules
    - initial sync itself is a 2nd-level feature, because
        1. if we initial sync the whole lot, things will still work (just more disk space will be used), and
        2. can "fake" an initial sync by restarting the secondary outside the replset and dropping the filtered namespaces

- similarly for rollback
    - do not use a filtered node as sync source when rolling back
    - if filtered, then when rolling back ignore anything on namespaces which have been filtered out


Config Constraints
-------------------

Filtered namespaces:

- no system db
- no local db
- no system collections
- must include admin db

Member config:

- not permitted on csrs
- must be priority 0
- must be hidden true
    - this can be relaxed as a separate later project, i.e. augment ismaster to show filters of non-hidden nodes. drivers could then take this into account when directing queries
- must be arbiterOnly: false
    - can never be relaxed, since arbiter machines have traditionally been specced to require no disk, this would catch people out
    - leaving regular arbiters in place permits backwards compatibility, and a good upgrade story.  if "arbiterOnly" is retired in a future version, that's a separate issue (and fine).  there could be a new option, eg. `arbiter: true` which is merely syntactic sugar for `votes: 1, filter: [ "admin" ]`, but it MUST NOT be named the same as the old `arbiterOnly` field, or else upgrades will be hell (worst case) or catch some people out (best case)
- initial implementation might require votes : 0
    - this would preclude an oplog-only member from being used as an "arbiter"
    - but it would significantly simplify things, since writes replicated by this host could never be included in w:majority

Reconfig:

- filter spec for a member cannot be changed on reconfig
    - later: allow the reconfig if the new set is a subset of the old set (maybe drop the lost namespaces, maybe not)
    - later: allow the set to increase, do a "mini-initial sync" of the new namespaces (super hard/advanced)


Example config
---------------

Simple filter spec:

    {
        "_id" : "replset",
        "version" : 1,
        "configsvr" : false,      // MANDATORY
        "protocolVersion" : NumberLong(1),  // PROBABLY MANDATORY
        "members" : [
            ...
            {
                "_id" : 1,
                "host" : "basique:12346",
                "arbiterOnly" : false,    // MANDATORY
                "buildIndexes" : true,
                "hidden" : true,          // MANDATORY
                "priority" : 0,           // MANDATORY
                "tags" : { },
                "slaveDelay" : NumberLong(0),
                "votes" : 1,              // THIS IS WHAT MAKES IT INTERESTING
                "filter" : [   // include these namespaces
                    "admin",   // MANDATORY
                    "wholedb",
                    "partialdb.somecollection"
                ]
            }
        ]
    }

More advanced filter spec:

    "filter" : [   // include these namespaces
        { "db" : "admin" },   // MANDATORY
        { "db" : "wholedb" },
        { "db" : "partialdb", "collection" : "somecollection" }
    ]


Much more advanced filter spec:

    "filter" : [
        { "include" : { "db" : "admin" } },   // MANDATORY
        { "include" : { "db" : "wholedb" } },
        { "include" : { "db" : "mostdb" } },
        { "exclude" : { "db" : "mostdb", "collection" : "badcollection" } },
        { "include" : { "db" : "partialdb", "collection" : "somecollection" } }
    ]


Motivation
-----------

- seed staging from prod sans pii
- backup pii separate from non pii
- mongodump some dbs with oplog/pit
- analytics
- analytics without pii
- oplog only nodes (esp massive oplog, or "growing" oplog without the disruption caused by existing oplog resize procedure)
- admin db (therefore auth) on "arbiters"
- mitosis
- finally nuke master-slave from orbit

- sharding is not a good solution to the above, because:
    - encourages micro sharding
    - no good way of moving whole dbs (movePrimary sucks)
    - the partitioning must be all or nothing, ie. disjoint, ie. non overlapping
    - requires choosing shard key, which is sometimes hard/impossible (e.g. id 1 has hot shard, id hashed has poor range queries, etc)
    - all the other sharded limitations (eg. no MR, no $lookup, etc etc etc)
    - mongos extra latency
    - not good when collections are dynamically created

