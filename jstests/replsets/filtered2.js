(function() {
    "use strict";

    load("jstests/replsets/filteredlib.js");

    jsTestLog(
        "START: Test a set which begins life without a filtered node, but then gets added immediately.");

    var rt = initReplsetWithoutFilteredNode("filtered2");
    addFilteredNode(rt);
    writeData(rt, {w: 1, wtimeout: 60 * 1000}, assert.writeOK);

    rt.awaitReplication();
    checkData(rt);
    checkOplogs(rt, 1);
    checkOplogs(rt, 2);

    testReplSetWriteConcernForSuccess(rt);

    rt.stopSet();
}());
