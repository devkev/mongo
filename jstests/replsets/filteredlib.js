// helpers for testing filtered replication functionality

load("jstests/replsets/rslib.js");

function initReplsetWithFilteredNode(name) {
    var rt = new ReplSetTest({name: name, nodes: 3});
    rt.startSet();
    rt.initiate({
        _id: name,
        members: [
            {_id: 0, host: rt.nodes[0].host, priority: 3},
            {_id: 1, host: rt.nodes[1].host, priority: 0},
            {
              _id: 2,
              host: rt.nodes[2].host,
              priority: 0,
              filter: ["admin", "included", "partial.included"]
            },
        ],
    });
    rt.waitForState(rt.nodes[0], ReplSetTest.State.PRIMARY);
    rt.awaitNodesAgreeOnPrimary();
    rt.awaitReplication();
    rt.numCopiesWritten = 0;
    return rt;
}

function initReplsetWithoutFilteredNode(name) {
    var rt = new ReplSetTest({name: name, nodes: 3});
    rt.startSet();
    rt.lastNodeOptions = rt.nodeOptions.n2;
    delete rt.nodeOptions.n2;
    rt.lastPort = rt.ports.pop();
    rt.lastNode = rt.nodes.pop();
    rt.initiate({
        _id: name,
        members: [
            {_id: 0, host: rt.nodes[0].host, priority: 3},
            {_id: 1, host: rt.nodes[1].host, priority: 0},
        ],
    });
    rt.waitForState(rt.nodes[0], ReplSetTest.State.PRIMARY);
    rt.awaitNodesAgreeOnPrimary();
    rt.awaitReplication();
    rt.numCopiesWritten = 0;
    // jsTestLog(tojson(rt));
    return rt;
}

function addFilteredNode(rt) {
    rt.awaitReplication();
    var cfg = rt.getReplSetConfigFromNode();
    cfg.version++;
    cfg.members.push({
        _id: 2,
        host: rt.lastNode.host,
        priority: 0,
        filter: ["admin", "included", "partial.included"]
    });
    rt.nodeOptions.n2 = rt.lastNodeOptions;
    rt.ports.push(rt.lastPort);
    rt.nodes.push(rt.lastNode);
    assert.commandWorked(rt.getPrimary().adminCommand({replSetReconfig: cfg}));
    rt.awaitNodesAgreeOnPrimary();
    rt.awaitReplication();
}

function initReplsetWithFilteredNodeAndArbiters(name) {
    var rt = new ReplSetTest({name: name, nodes: 5, oplogSize: 2, useBridge: true});
    rt.startSet();
    rt.initiate({
        _id: name,
        members: [
            {_id: 0, host: rt.nodes[0].host, priority: 3},
            {_id: 1, host: rt.nodes[1].host, priority: 2},
            {
              _id: 2,
              host: rt.nodes[2].host,
              priority: 0,
              filter: ["admin", "included", "partial.included"]
            },
            {_id: 3, host: rt.nodes[3].host, arbiterOnly: true},
            {_id: 4, host: rt.nodes[4].host, arbiterOnly: true},
        ],
    });
    rt.waitForState(rt.nodes[0], ReplSetTest.State.PRIMARY);
    rt.awaitNodesAgreeOnPrimary();
    rt.awaitReplication();
    rt.numCopiesWritten = 0;
    return rt;
}

// Force node "node" to sync from node "from".
function syncNodeFrom(rt, node, from) {
    jsTestLog(tojson(assert.commandWorked(
        rt.nodes[node].getDB("admin").runCommand({"replSetSyncFrom": rt.nodes[from].host}))));
    var res;
    assert.soon(
        function() {
            res = rt.nodes[node].getDB("admin").runCommand({"replSetGetStatus": 1});
            return res.syncingTo === rt.nodes[from].host;
        },
        function() {
            return "node " + node + " failed to start syncing from node " + from + ": " +
                tojson(res);
        });
}

// Force node 1 (regular node) to sync from node 2 (filtered node).
function normalNodeSyncFromFilteredNode(rt) {
    // Have to first force the filtered node to sync from the primary, in case it happens to have
    // been syncing from the regular node.
    syncNodeFrom(rt, 2, 0);
    syncNodeFrom(rt, 1, 2);
}

// FIXME: stolen from src/mongo/shell/replsettest.js
// FIXME: make it public in ReplSetTest ffs
/**
 * Returns the optime for the specified host by issuing replSetGetStatus.
 */
function _getLastOpTime(conn) {
    var replSetStatus =
        assert.commandWorked(conn.getDB("admin").runCommand({replSetGetStatus: 1}));
    var connStatus = replSetStatus.members.filter(m => m.self)[0];
    return connStatus.optime;
}

function _getConnStatus(conn) {
    var replSetStatus =
        assert.commandWorked(conn.getDB("admin").runCommand({replSetGetStatus: 1}));
    var connStatus = replSetStatus.members.filter(m => m.self)[0];
    return connStatus;
}

var excludedNamespaces = ["excluded.excluded", "partial.excluded"];
var includedNamespaces = ["included.included", "partial.included"];
var bothNamespaces = [].concat(excludedNamespaces).concat(includedNamespaces);

// Write some data.
function writeData(rt, writeConcern, expectedResult) {
    var primary = rt.getPrimary();
    var options = {writeConcern};
    bothNamespaces.forEach((ns) =>
                               expectedResult(primary.getCollection(ns).insert({ns, n: rt.numCopiesWritten}, options)));
    rt.numCopiesWritten++;
}

// The regular node should have everything.
function checkUnfilteredData(rt) {
    bothNamespaces.forEach((ns) => assert.eq(rt.numCopiesWritten,
                                             rt.nodes[1].getCollection(ns).find({ns}).count()));
    bothNamespaces.forEach(
        (ns) => rt.nodes[1].getCollection(ns).find({ns}).forEach((doc) => assert.eq(ns, doc.ns)));
}

function checkUnfilteredDataParticularWrite(rt, writeNum) {
    bothNamespaces.forEach((ns) => assert.eq(1,
                                             rt.nodes[1].getCollection(ns).find({ns, n: writeNum}).count()));
    bothNamespaces.forEach(
        (ns) => rt.nodes[1].getCollection(ns).find({ns, n: writeNum}).forEach((doc) => assert.eq(writeNum, doc.n)));
}

function checkUnfilteredDataMissingParticularWrite(rt, writeNum) {
    bothNamespaces.forEach((ns) => assert.eq(0,
                                             rt.nodes[1].getCollection(ns).find({ns, n: writeNum}).count()));
}

// The filtered node should only have the included things, and none of the excluded things.
function checkFilteredData(rt) {
    excludedNamespaces.forEach(
        (ns) => assert.eq(0, rt.nodes[2].getCollection(ns).find({ns}).count()));
    excludedNamespaces.forEach((ns) => assert.eq(0, rt.nodes[2].getCollection(ns).find().count()));
    excludedNamespaces.forEach((ns) => assert.eq(0, rt.nodes[2].getCollection(ns).count()));
    includedNamespaces.forEach(
        (ns) =>
            assert.eq(rt.numCopiesWritten, rt.nodes[2].getCollection(ns).find({ns}).count()));
    includedNamespaces.forEach(
        (ns) => rt.nodes[2].getCollection(ns).find({ns}).forEach((doc) => assert.eq(ns, doc.ns)));
}

function checkFilteredDataParticularWrite(rt, writeNum) {
    includedNamespaces.forEach(
        (ns) =>
            assert.eq(1, rt.nodes[2].getCollection(ns).find({ns, n: writeNum}).count()));
    includedNamespaces.forEach(
        (ns) => rt.nodes[2].getCollection(ns).find({ns, n: writeNum}).forEach((doc) => assert.eq(writeNum, doc.n)));
}

function checkFilteredDataMissingParticularWrite(rt, writeNum) {
    includedNamespaces.forEach((ns) => assert.eq(0,
                                                 rt.nodes[2].getCollection(ns).find({ns, n: writeNum}).count()));
}

// Check that all the data is where it should be.
function checkData(rt) {
    checkUnfilteredData(rt);
    checkFilteredData(rt);
}

function checkDataParticularWrite(rt, writeNum) {
    checkUnfilteredDataParticularWrite(rt, writeNum);
    checkFilteredDataParticularWrite(rt, writeNum);
}

function checkDataMissingParticularWrite(rt, writeNum) {
    checkUnfilteredDataMissingParticularWrite(rt, writeNum);
    checkFilteredDataMissingParticularWrite(rt, writeNum);
}

// Check that the oplogs are how they should be.
function checkOplogs(rt, nodeNum) {
    rt.getPrimary()
        .getDB("local")
        .oplog.rs.find({op: {$ne: "n"}})
        .sort({$natural: 1})
        .forEach(function(op) {
            checkOpInOplog(rt.nodes[nodeNum], op, 1);
        });
}

function testReplSetWriteConcern(f) {
    [2, "majority"].forEach((w) => {
        [false, true].forEach((j) => {
            jsTestLog("Write concern: " + tojson({w, j}));
            f(w, j);
        });
    });
}

function testReplSetWriteConcernForFailure(rt) {
    testReplSetWriteConcern((w, j) => {
        writeData(rt,
                  {w, j, wtimeout: 5 * 1000},
                  (x) => assert.eq(true,
                                   assert.writeErrorWithCode(x, ErrorCodes.WriteConcernFailed)
                                       .getWriteConcernError()
                                       .errInfo.wtimeout));
        // FIXME: need to wait for partial repl
        //rt.awaitReplication();
        checkFilteredData(rt);
        checkOplogs(rt, 2);
    });
}

function testReplSetWriteConcernForSuccess(rt, checkFilteredOplogs) {
    if (typeof(checkFilteredOplogs) == "undefined") {
        checkFilteredOplogs = true;
    }
    testReplSetWriteConcern((w, j) => {
        writeData(rt, {w, j, wtimeout: 60 * 1000}, assert.writeOK);
        rt.awaitReplication();
        checkData(rt);
        checkOplogs(rt, 1);
        if (checkFilteredOplogs) {
            checkOplogs(rt, 2);
        }
    });
}
