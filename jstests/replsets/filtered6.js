(function() {
    "use strict";

    load("jstests/replsets/filteredlib.js");

    jsTestLog("START: Test rollback of/with filtered node.");

    /* Overview:
        1. PSFAA replset
           * A = "Primary"
           * B = "Secondary"
           * C = Filtered Node
           * D = Arbiter
           * E = Arbiter

        2. Do some writes on A (Writes1)
        3. Wait for writes to replicate to B and C

        4. Network partition A/BCDE
        5. B will become primary
        6. Do some writes on B (Writes2)
        7. Wait for writes to replicate to C

        8. Network partition BC/ADE
           8.1. B will stepdown
           8.2. A will be elected primary (but unaware of the writes on B)
        9. Do some writes on A (Writes3)

       10. Let C now be able to see ADE
           10.1. C will rollback
                 10.1.1. Should not fetch docs on excluded namespaces

       11. Force B to syncFrom C
           11.1. B will want to rollback, but should not like C as a sync source
                 11.1.1. Switch sync source to A

       12. Restore the network
           12.1. B should be able to complete rollback from A

       13. Confirm that:
           13.1. All nodes can see the data that's they're supposed to (Writes1 and Writes3)
           13.2. No nodes can see the data that's been rolled back (Writes2)
           13.3. All nodes have identical oplogs
     */


    jsTestLog("1. PSFAA replset");
    var rt = initReplsetWithFilteredNodeAndArbiters("filtered6");

    jsTestLog("2. Do some writes on A (Writes1)");
    writeData(rt, {w: 1, wtimeout: 60 * 1000}, assert.writeOK);

    jsTestLog("3. Wait for writes to replicate to B and C");
    rt.awaitReplication();
    checkData(rt);
    checkOplogs(rt, 1);
    checkOplogs(rt, 2);

    jsTestLog("4. Network partition A/BCDE");
    rt.nodes[0].disconnect(rt.nodes[1]);
    rt.nodes[0].disconnect(rt.nodes[2]);
    rt.nodes[0].disconnect(rt.nodes[3]);
    rt.nodes[0].disconnect(rt.nodes[4]);

    jsTestLog("5. B will become primary");
    rt.waitForState(rt.nodes[1], ReplSetTest.State.PRIMARY);

    jsTestLog("6. Do some writes on B (Writes2)");
    // FIXME: each set of writes need to be distinct, so that we can confirm at the end which ones
    // are actually there (when there have been multiple writes).
    writeData(rt, {w: 1, wtimeout: 60 * 1000}, assert.writeOK);

    jsTestLog("7. Wait for writes to replicate to C");
    var primaryOpTime = _getLastOpTime(rt.getPrimary());
    assert.soon(() => (friendlyEqual(primaryOpTime, _getLastOpTime(rt.nodes[2]))));

    // FIXME: check data as well as oplogs
    // FIXME
    //sleep(1000);
    checkOplogs(rt, 2);

    jsTestLog("8. Network partition BC/ADE");
    // Disconnects first, then reconnects
    //rt.nodes[0].disconnect(rt.nodes[1]);  // already the case
    //rt.nodes[0].disconnect(rt.nodes[2]);  // already the case
    rt.nodes[3].disconnect(rt.nodes[1]);
    rt.nodes[3].disconnect(rt.nodes[2]);
    rt.nodes[4].disconnect(rt.nodes[1]);
    rt.nodes[4].disconnect(rt.nodes[2]);
    rt.nodes[0].reconnect(rt.nodes[3]);
    rt.nodes[0].reconnect(rt.nodes[4]);

    jsTestLog("8.1. B will stepdown");
    // Can't use waitForState because of the network partition (it always goes based on what the
    // primary sees).
    //rt.waitForState(rt.nodes[1], ReplSetTest.State.SECONDARY);
    assert.soon(() => {
        try {
            return (_getConnStatus(rt.nodes[1]).state == ReplSetTest.State.SECONDARY);
        } catch(ex) {
            return false;
        }
    });

    jsTestLog("8.2. A will be elected primary (but unaware of the writes on B)");
    rt.waitForState(rt.nodes[0], ReplSetTest.State.PRIMARY);

    jsTestLog("9. Do some writes on A (Writes3)");
    writeData(rt, {w: 1, wtimeout: 60 * 1000}, assert.writeOK);

    jsTestLog("10. Let C now be able to see ADE");
    rt.nodes[0].reconnect(rt.nodes[2]);
    rt.nodes[3].reconnect(rt.nodes[2]);
    rt.nodes[4].reconnect(rt.nodes[2]);

    jsTestLog("10.1. C will rollback");
    jsTestLog("10.1.1. Should not fetch docs on excluded namespaces");
    assert.soon(() => {
        try {
            return (_getConnStatus(rt.nodes[2]).state == ReplSetTest.State.SECONDARY);
        } catch(ex) {
            return false;
        }
    });

    // FIXME: fix and re-enable this
    //jsTestLog("11. Force B to syncFrom C");
    //sleep(10000);
    //assert.soonNoExcept(() => {
    //rt.nodes[1].getDB("admin").runCommand("ping");
    //rt.nodes[1].getDB("admin").runCommand("ping");
    //rt.nodes[1].getDB("admin").runCommand("ping");
    //rt.nodes[1].getDB("admin").runCommand("ping");
    //rt.nodes[2].getDB("admin").runCommand("ping");
    //rt.nodes[2].getDB("admin").runCommand("ping");
    //rt.nodes[2].getDB("admin").runCommand("ping");
    //rt.nodes[2].getDB("admin").runCommand("ping");
    //return true;
    //});
    //syncNodeFrom(rt, 1, 2);

    //jsTestLog("11.1. B will want to rollback, but should not like C as a sync source");
    //jsTestLog("11.1.1. Switch sync source to A (but it can't reach A)");
    //assert.soon(() => {
    //    try {
    //        return (_getConnStatus(rt.nodes[1]).state == ReplSetTest.State.ROLLBACK);
    //    } catch(ex) {
    //        return false;
    //    }
    //});

    jsTestLog("12. Restore the network");
    rt.nodes[0].reconnect(rt.nodes[1]);
    rt.nodes[3].reconnect(rt.nodes[1]);
    rt.nodes[4].reconnect(rt.nodes[1]);

    jsTestLog("12.1. B should be able to complete rollback from A");
    assert.soon(() => {
        try {
            return (_getConnStatus(rt.nodes[1]).state == ReplSetTest.State.SECONDARY);
        } catch(ex) {
            return false;
        }
    });


    rt.awaitReplication();

    jsTestLog("13.1. Confirm that: All nodes can see the data that's they're supposed to (Writes1 and Writes3)");
    rt.numCopiesWritten--;
    checkData(rt);
    // just Writes1 and Writes3
    checkDataParticularWrite(rt, 0);
    checkDataParticularWrite(rt, 2);

    jsTestLog("13.2. Confirm that: No nodes can see the data that's been rolled back (Writes2)");
    // not Writes2
    checkDataMissingParticularWrite(rt, 1);

    jsTestLog(tojson(bothNamespaces.map((coll) => (rt.nodes[0].getCollection(coll).find().toArray()))));
    jsTestLog(tojson(bothNamespaces.map((coll) => (rt.nodes[1].getCollection(coll).find().toArray()))));
    jsTestLog(tojson(bothNamespaces.map((coll) => (rt.nodes[2].getCollection(coll).find().toArray()))));

    jsTestLog("13.3. Confirm that: All nodes have identical oplogs");
    checkOplogs(rt, 1);
    checkOplogs(rt, 2);

    rt.stopSet();
}());
