// -*- LSST-C++ -*-

#include "boost/format.hpp"

#include "lsst/qserv/master/xrdfile.h"
#include "lsst/qserv/master/dispatcher.h"
#include "lsst/qserv/master/thread.h"

namespace qMaster = lsst::qserv::master;

namespace {
    std::string makeUrl(int chunk) {
	char* hostport = ::getenv("QSERV_XRD");
	if(hostport == NULL) {
	    hostport = "lsst-dev01:1094";
	}
	char* user = "qsmaster";
	boost::format f("xroot://%s@%s//query/%d");
	return (f % user % hostport % chunk).str();
    }
    qMaster::QueryManager& getManager(int session) {
	// Singleton for now. //
	static boost::shared_ptr<qMaster::QueryManager> qm;
	if(qm.get() == NULL) {
	    qm = boost::make_shared<qMaster::QueryManager>();
	}
	return *qm;
    }
}

void qMaster::initDispatcher() {
    xrdInit();
}

/// @param session Int for the session (the top-level query)
/// @param chunk Chunk number within this session (query)
/// @param str Query string (c-string)
/// @param len Query string length
/// @param savePath File path (with name) which will store the result (file, not dir)
/// @return a token identifying the session??? FIXME
int qMaster::submitQuery(int session, int chunk, char* str, int len, char* savePath) {
    TransactionSpec t;
    t.path = savePath;
    t.query = std::string(str, len);
    t.bufferSize = 8192000;
    t.path = makeUrl(chunk);
    return submitQuery(session, TransactionSpec(t));
}

int qMaster::submitQuery(int session, qMaster::TransactionSpec const& s) {
    QueryManager& qm = getManager(session);
    int queryId = 0;
    /* queryId = */ qm.add(s); 
    return queryId;
}

qMaster::QueryState qMaster::joinQuery(int session, int id) {
    // Block until specific query id completes.
    QueryManager& qm = getManager(session);
    qm.join(id);
    qm.status(id); // get status
    // If error, report
    return UNKNOWN; // FIXME: convert status to querystate.
}

qMaster::QueryState qMaster::tryJoinQuery(int session, int id) {
    QueryManager& qm = getManager(session);
    // Just get the status and return it.
    if(qm.tryJoin(id)) {
	return SUCCESS; 
    } else {
	return ERROR;
    }   
}

qMaster::QueryState qMaster::joinSession(int session) {
    // FIXME
    QueryManager& qm = getManager(session);
    qm.joinEverything();
}

int qMaster::newSession() {
    return 1; // For now, always give session # 1
}

void qMaster::discardSession(int session) {
    // FIXME
    return; // For now, don't discard.
}

qMaster::XrdTransResult qMaster::getQueryResult(int session, int chunk) {
    // FIXME
}
