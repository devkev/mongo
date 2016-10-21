(function() {
    "use strict";

    load("jstests/replsets/filteredlib.js");

    jsTestLog(
        "START: Test a set which begins life without a filtered node, but then gets added later, ie. initial sync.");

    var rt = initReplsetWithoutFilteredNode("filtered3");
    writeData(rt, {w: 1, wtimeout: 60 * 1000}, assert.writeOK);
    addFilteredNode(rt);

    rt.awaitReplication();
    checkData(rt);
    checkOplogs(rt, 1);

    testReplSetWriteConcernForSuccess(rt, false);

    rt.stopSet();
}());
