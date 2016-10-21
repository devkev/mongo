(function() {
    "use strict";

    load("jstests/replsets/filteredlib.js");

    jsTestLog("START: Test a set which begins life with a filtered node.");

    var rt = initReplsetWithFilteredNode("filtered1");
    writeData(rt, {w: 1, wtimeout: 60 * 1000}, assert.writeOK);

    rt.awaitReplication();
    checkData(rt);
    checkOplogs(rt, 1);
    checkOplogs(rt, 2);

    testReplSetWriteConcernForSuccess(rt);

    rt.stopSet();
}());
