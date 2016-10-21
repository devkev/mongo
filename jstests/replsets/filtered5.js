(function() {
    "use strict";

    load("jstests/replsets/filteredlib.js");

    jsTestLog("START: Test syncing oplog via filtered node (ie. repl chaining).");

    var rt = initReplsetWithFilteredNode("filtered5");
    writeData(rt, {w: 1, wtimeout: 60 * 1000}, assert.writeOK);

    rt.awaitReplication();
    checkData(rt);
    checkOplogs(rt, 1);
    checkOplogs(rt, 2);

    normalNodeSyncFromFilteredNode(rt);

    // Do a write and make sure it propagates correctly.
    writeData(rt, {w: 1, wtimeout: 60 * 1000}, assert.writeOK);
    rt.awaitReplication();
    checkData(rt);
    checkOplogs(rt, 1);
    checkOplogs(rt, 2);

    // Likewise for write concern writes (confirm replSetUpdatePosition propagation).
    testReplSetWriteConcernForSuccess(rt);

    rt.stopSet();
}());
