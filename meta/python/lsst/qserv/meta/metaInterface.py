#!/usr/bin/env python

# LSST Data Management System
# Copyright 2008, 2009, 2010 LSST Corporation.
# 
# This product includes software developed by the
# LSST Project (http://www.lsst.org/).
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the LSST License Statement and 
# the GNU General Public License along with this program.  If not, 
# see <http://www.lsstcorp.org/LegalNotices/>.

"""
This module defines the interface that qserv metadata server presents to 
the outside world.
"""


# Standard
from itertools import ifilter
import logging

# Package imports
import config
from metaImpl import MetaImpl
from status import Status, QmsException

# Interface for qserv metadata server
class MetaInterface:
    def __init__(self):
        loggerName = "qmsLogger"
        self._initLogging(loggerName)
        self._metaImpl = MetaImpl(loggerName)

        okname = ifilter(lambda x: "_" not in x, dir(self))
        self.publishable = filter(lambda x: hasattr(getattr(self,x), 
                                                    'func_doc'), 
                                  okname)

    def _x1(self, f, *args):
        """Function that accepts function objects and arbitrary number of
           arguments that should be passed to the passed function. It returns
           error status."""
        try:
            f(*args)
        except QmsException as qe: 
            if qe.getErrNo() == Status.ERR_DB_EXISTS:
                return Status.ERR_META_EXISTS
            return qe.getErrNo()
        except Exception, e:
            self._logger.error("Exception in %s: %s" % (f.func_name, str(e)))
            return Status.ERR_INTERNAL
        return Status.SUCCESS

    def _x2(self, f, *args):
        """Function that accepts function objects and arbitrary number of
           arguments that should be passed to the passed function. It returns
           value and the error status."""
        try:
            retV = f(*args)
        except QmsException as qe: 
            self._logger.error("Exception in %s: %s" % \
                                   (f.func_name, qe.getErrMsg()))
            return (qe.getErrNo(), "")
        except Exception, e:
            self._logger.error("Exception in %s: %s" % (f.func_name, str(e)))
            return (Status.ERR_INTERNAL, "")
        return (Status.SUCCESS, retV)

    def installMeta(self):
        """Initializes qserv metadata. It creates persistent structures,
        (it should be called only once)."""
        return self._x1(self._metaImpl.installMeta)

    def destroyMeta(self):
        """Permanently destroys qserv metadata."""
        return self._x1(self._metaImpl.destroyMeta)

    def printMeta(self):
        """Returns string that contains all metadata."""
        return self._x2(self._metaImpl.printMeta)

    def createDb(self, dbName, crDbOptions):
        """Creates metadata about new database to be managed by qserv."""
        return self._x1(self._metaImpl.createDb, dbName, crDbOptions)

    def dropDb(self, dbName):
        """Removes metadata about a database managed by qserv."""
        return self._x1(self._metaImpl.dropDb, dbName)

    def retrieveDbInfo(self, dbName):
        """Retrieves information about a database managed by qserv."""
        return self._x2(self._metaImpl.retrieveDbInfo, dbName)

    def checkDbExists(self, dbName):
        """Checks if db <dbName> exists, returns 0 (no) or 1 (yes)."""
        return self._x2(self._metaImpl.checkDbExists, dbName)

    def listDbs(self):
        """Returns string that contains list of databases managed by qserv."""
        return self._x2(self._metaImpl.listDbs)

    def createTable(self, dbName, crTbOptions, schemaStr):
        """Creates metadata about new table from qserv-managed database."""
        return self._x1(self._metaImpl.createTable, 
                        dbName, crTbOptions, schemaStr)

    def dropTable(self, dbName, tableName):
        """Removes metadata about a table."""
        return self._x1(self._metaImpl.dropTable, dbName, tableName)

    def listTables(self, dbName):
        """Retrieves a list of all tables for a given database."""
        return self._x2(self._metaImpl.listTables, dbName, False)

    def listPartTables(self, dbName):
        """Retrieves a list of partitioned tables for a given database."""
        return self._x2(self._metaImpl.listTables, dbName, True)

    def retrieveTableInfo(self, dbName, tableName):
        """Retrieves information about a table."""
        return self._x2(self._metaImpl.retrieveTableInfo, dbName, tableName)

    def getInternalQmsDbName(self):
        """Retrieves name of the internal qms database. """
        return self._x2(self._metaImpl.getInternalQmsDbName)

    def help(self):
        """A brief help message showing available commands."""
        r = "" ## self._handyHeader()
        r += "\n<pre>Available qms commands:\n"
        sorted =  map(lambda x: (x, getattr(self, x)), self.publishable)
        sorted.sort()
        for (k,v) in sorted:
            r += "%-20s : %s\n" %(k, v.func_doc)
        r += "</pre>\n"
        return r

    def _initLogging(self, loggerName):
        outFile = config.config.get("logging", "outFile")
        levelName = config.config.get("logging", "level")
        if levelName is None:
            level = logging.ERROR # default
        else:
            ll = {"debug":logging.DEBUG,
                  "info":logging.INFO,
                  "warning":logging.WARNING,
                  "error":logging.ERROR,
                  "critical":logging.CRITICAL}
            level = ll[levelName]
        self._logger = logging.getLogger(loggerName)
        hdlr = logging.FileHandler(outFile)
        formatter = logging.Formatter('%(asctime)s %(levelname)s %(message)s')
        hdlr.setFormatter(formatter)
        self._logger.addHandler(hdlr) 
        self._logger.setLevel(level)