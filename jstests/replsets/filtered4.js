(function() {
    "use strict";

    load("jstests/replsets/filteredlib.js");

    jsTestLog("START: Test that filtered nodes don't contribute to write concern.");

    var rt = initReplsetWithFilteredNode("filtered4");
    writeData(rt, {w: 1, wtimeout: 60 * 1000}, assert.writeOK);

    rt.awaitReplication();
    checkData(rt);
    checkOplogs(rt, 1);
    checkOplogs(rt, 2);

    rt.stop(1);
    testReplSetWriteConcernForFailure(rt);

    rt.stopSet();
}());
