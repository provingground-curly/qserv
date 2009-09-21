
from collections import defaultdict
from itertools import imap
import string

# Extended from Paul McGuire's simpleSQL.py which was a sample from
# the pyparsing project ( http://pyparsing.wikispaces.com/ )
# Some changes:
# support for BETWEEN in WHERE expressions
# support for aliasing.  AS keyword is required, otherwise pyparsing
# mistakes "FROM" for an alias (pyparsing is not recursive descent). 

from pyparsing import \
    Literal, CaselessLiteral, Word, Upcase,\
    delimitedList, Optional, Combine, Group, alphas, nums, \
    alphanums, ParseException, Forward, oneOf, quotedString, \
    ZeroOrMore, restOfLine, Keyword, upcaseTokens, Or

class Grammar:

    def actionWrapper(self, aList):
       """Creates a parseAction whose actions are changeable
       after the grammar is defined.
       """
       def action(tokens):
          for a in aList:
             tokens = a(tokens)
          return tokens
       return action

    def __init__(self):
        # define SQL tokens
        selectStmt = Forward()
        selectToken = Keyword("select", caseless=True)
        fromToken   = Keyword("from", caseless=True)
        asToken   = Keyword("as", caseless=True)
        whereToken = Keyword("where", caseless=True)
        semicolon = Literal(";")

        ident = Word( alphas, alphanums + "_$" ).setName("identifier")

        columnName     = delimitedList( ident, ".", combine=True )
        #columnName.setParseAction(upcaseTokens)
        columnNameList = Group( delimitedList( columnName ) )

        functionExpr = ident + Literal('(') + columnNameList + Literal(')')
        alias = Forward()
        identExpr  = functionExpr | ident
        self.identExpr = identExpr
        self.functionExpr = functionExpr
        alias = ident.copy()

        selectableList = Group( delimitedList(identExpr | columnName ))
        columnRef = columnName
        functionSpec = functionExpr
        valueExprPrimary = functionSpec | columnRef
        numPrimary = valueExprPrimary ## | numericValFunc
        factor = Optional(Literal("+") | Literal("-")) + numPrimary
        term = Forward()
        term = factor | (term + Literal("*") + factor) | (term + Literal("/") + factor)
        numericExpr = Forward()
        numericExpr = term | (numericExpr + Literal('+') + term) | (numericExpr + Literal('-') + term)
        valueExpr = numericExpr ## | stringExpr | dateExpr | intervalExpr
        derivedColumn = valueExpr + Optional(asToken + alias)
        selectSubList = delimitedList(derivedColumn)


        tableName      = delimitedList( ident, ".", combine=True )
        # don't upcase table names anymore
        # tableName.setParseAction(upcaseTokens) 
        self.tableAction = []
        tableName.addParseAction(self.actionWrapper(self.tableAction))
        tableName.setResultsName("table")
        tableAlias = tableName + asToken + ident.setResultsName("aliasName")
        tableAlias.setResultsName("alias")
        genericTableName = tableAlias | tableName
        genericTableName = genericTableName.setResultsName("tablename")
        tableNameList  = Group( delimitedList( genericTableName ) )

        whereExpression = Forward()
        and_ = Keyword("and", caseless=True)
        or_ = Keyword("or", caseless=True)
        in_ = Keyword("in", caseless=True)
        between_ = Keyword("between", caseless=True)
    
        E = CaselessLiteral("E")
        binop = oneOf("= != < > >= <= eq ne lt le gt ge", caseless=True)
        arithSign = Word("+-",exact=1)
        realNum = Combine( Optional(arithSign) + ( Word( nums ) + "." 
                                                   + Optional( Word(nums) )  |
                                                   ( "." + Word(nums) ) ) + 
                           Optional( E + Optional(arithSign) + Word(nums) ) )
        intNum = Combine( Optional(arithSign) + Word( nums ) + 
                          Optional( E + Optional("+") + Word(nums) ) )

        # need to add support for alg expressions
        columnRval = realNum | intNum | quotedString | columnName 

        self.whereExpAction = []

        whereConditionFlat = Group(
            ( columnName + binop + columnRval ) |
            ( columnName + in_ + "(" + delimitedList( columnRval ) + ")" ) |
            ( columnName + in_ + "(" + selectStmt + ")" ) |
            ( columnName.setResultsName("column") 
              + between_ + columnRval + and_ + columnRval ) 
            )
        whereConditionFlat.addParseAction(self.actionWrapper(self.whereExpAction))
        whereCondition = Group(whereConditionFlat 
                               | ( "(" + whereExpression + ")" ))

        #whereExpression << whereCondition.setResultsName("wherecond")
        #+ ZeroOrMore( ( and_ | or_ ) + whereExpression ) 
        def scAnd(tok):
            print "scAnd", tok
            if "TRUE" == tok[0][0]:
                tok = tok[2] 
            elif "TRUE" == tok[2][0]:
                tok = tok[0]
                
            return tok
        def scOr(tok):
            print "scOr", tok
            if ("TRUE" == tok[0][0]) or ("TRUE" == tok[2][0]):
                tok = [["TRUE"]]
            return tok
        def scWhere(tok):
            newtok = []
            i = 0
            while i < len(tok):
                if str(tok[i]) in ["TRUE",str(["TRUE"])] and (i+1) < len(tok):
                    if str(tok[i+1]).upper() == "AND":
                        i += 2
                        continue
                    elif str(tok[i+i]).upper() == "OR":
                        break
                newtok.append(tok[i])
                i += 1
            return newtok
        
        def collapseWhere(tok):
            #collapse.append(tok[0][1])
            if ["TRUE"] == tok.asList()[0][1]:
                tok = [] 
            return tok
        andExpr = and_ + whereExpression
        orExpr = or_ + whereExpression
        whereExpression << whereCondition + ZeroOrMore(
            andExpr | orExpr)
        whereExpression.addParseAction(scWhere)

        self.selectPart = selectToken + ( '*' | selectSubList ).setResultsName( "columns" )
        whereClause = Group(whereToken + 
                            whereExpression).setResultsName("where") 
        whereClause.addParseAction(collapseWhere)
        self.fromPart = fromToken + tableNameList.setResultsName("tables")

        # define the grammar
        selectStmt      << ( self.selectPart +                         
                             fromToken + 
                             tableNameList.setResultsName( "tables" ) +
                             whereClause)
        
        self.simpleSQL = selectStmt + semicolon

        # define Oracle comment format, and ignore them
        oracleSqlComment = "--" + restOfLine
        self.simpleSQL.ignore( oracleSqlComment )


class QueryMunger:
    def __init__(self, query):
        self.original = query
        pass
    

    def disasmBetween(self, tokList):
        x = tokList
        return {"col" : x[0],
                "min" : x[2],
                "max" : x[4]}

    def convertBetween(self, tokList):
        (col, dum, cmin, dum_, cmax) = tokList
        clist = ["%s between %smin and %smax" % (cmin, col, col),
                 "%s between %smin and %smax" % (cmax, col, col),
                 "%smin between %s and %s" % (col, cmin, cmax)]
        return "(%s)" % " OR ".join(clist)
        
    def computeChunkQuery(self, chunk, sublist):
        print "chunk query for c=", chunk, " sl=", sublist 
        g = Grammar()
        def replaceObj(tokens):
            for i in range(len(tokens)):
                if tokens[i].upper() == "OBJECT":
                    tokens[i] = "Subchunks_${subc}.Object_${chunk}_${subc}"
            
        g.tableAction.append(replaceObj)
        blah = g.simpleSQL.parseString(self.original)
        print "sublist", sublist
        header = '-- SUBCHUNKS:' + ", ".join(imap(str,sublist))
        querytemplate = string.Template(self._flatten(blah) + ";")
        chunkqueries = [querytemplate.substitute({'chunk': chunk, 'subc':s}) for s in sublist]
        return "\n".join([header] + chunkqueries)

    def collectSubChunkTuples(self, chunktuples):
        # Use dictionary to collect tuples by chunk #.
        d = defaultdict(list)
        for chunk, subchunk in chunktuples:
            d[chunk].append(subchunk)
        
        return d
    
    def computePartMapQuery(self):
        g = Grammar()
        pVariables = set(["RA", "DECL"])
        tableList = []
        whereList = []

        def disasm(tokens):
            x = tokens[0] # Unnest
            if x[1].upper() == "BETWEEN":
                return self.disasmBetween(x)
        def convert(tokens):
            x = tokens[0] # Unnest
            if x[1].upper() == "BETWEEN":
                return self.convertBetween(x)
            return tokens
            
        # Track the where expressions
        def accuWhere(tok):
            whereList.append(tok)
            return tok
        def compose(tok):
            return convert(tok)
            
        def onlyStatic(tok):
            condition = tok[0]
            def partMapCares(token):
                return token.upper() in pVariables
            if not filter(partMapCares, condition):
                tok[0] = "TRUE";
            return tok
        g.whereExpAction.append(onlyStatic)
 
        #print self.original, "BEFORE_____"
        t = g.simpleSQL.parseString(self.original, parseAll=True)
        #print self.original, "AFTER_____"
        #flatTokens = self._flatten(t)
        flatWhere = self._flatten(t.where)
        
        pquery = "SELECT chunkid,subchunkid FROM partmap %s;" % flatWhere
        
        return pquery

    def _flatten(self, l):
      if isinstance(l, str): return l
      else: return " ".join(map(self._flatten, l))

    pass


def getTokens(qstr):
    g = Grammar()
    whereList = []
    tableList = []
    def accumulateWhere(tok):
        whereList.append(tok)
    def accumulateTable(tok):
        tableList.append(tok)
    def alterTable(tok):
        return ["partmap"]
    g.tableAction.append(accumulateTable)
    g.whereExpAction.append(accumulateWhere)
    t = g.simpleSQL.parseString(qstr)
    def flatten(l):
        if isinstance(l, str):
            return l
        else:
            return " ".join(map(flatten, l))

    flatTokens = flatten(t)
    flatWhere = flatten(t.where)
    flatFirst = flatTokens[:flatTokens.find(flatWhere)]
    flatTable = flatten(tableList)
    print "unflat----where[1]---", t.where[0][1]
    print "whereflatten", flatWhere 
    print "wherelist---", whereList
    print "flatFirst", flatFirst
    flatMunge = flatFirst[:flatFirst.find(flatTable)]
    print "flatmunge", flatMunge
    pquery = "SELECT chunkid,subchunkid FROM partmap %s;" % flatten(t.where)
    print pquery
    chunkquery = "%s %s%s" % (flatMunge, flatTable, "_%d_%d"%(12,42))
    print chunkquery
    blog = 0
    for i in whereList:
        print blog, i
        blog += 1
    return t



def test(qstr):
    g = Grammar()
    print qstr,"->"
    try:
        tokens = g.simpleSQL.parseString(qstr)
        print "tokens = ",        tokens
        print "tokens.columns =", tokens.columns
        print "tokens.tables =",  tokens.tables
        print "tokens.where =", tokens.where
        print tokens.column
    except ParseException, err:
        print " "*err.loc + "^\n" + err.msg
        print err
    print
    pass



# For lspeed, we need to take range specs for ra and decl and emit
# specs that can be applied against our chunk table map.
# chunk table schema (tentative)
# CREATE TABLE chunkmap (chunkid int, 
#                        subchunkId int, 
#                        nodeId int,
#                        ramin float, ramax float
#                        declmin float, declmax float );

def findLocationFromQuery(query):
    """"return tuples of (chunkid, subchunkid, nodeid) from a
    bounding-box query"""

    # parse query
    # extract WHERE clause for its RA and DECL ranges
    # apply ranges to chunkmap table to find out:
    # 1. what chunks are needed (and what subchunks are needed)
    # 2. where these chunk/subchunks are.
    # the end goal is that we want to take the original query,
    # and fan it out to the different nodes.  each query-replicant is
    # tagged with what chunk and subchunk it concerns, and is grouped
    # by chunk and node.
    pass

qs=[ """SELECT o1.id,o2.id,spdist(o1.ra, o1.decl, o2.ra, o2.decl) 
  AS dist FROM Object AS o1, Object AS o2 WHERE dist < 25 AND o1.id != o2.id;""",
     """SELECT id FROM Object where ra between 2 and 5 AND blah < 3 AND decl > 4;""", 
     """SELECT id FROM Object where blah < 3 AND decl > 4;""" ]
def mytest():
    for q in qs:
        qm = QueryMunger(q)
        s = qm.computePartMapQuery()
        print "pmquery=", s
    pass

if __name__ == '__main__':
    quer = "select * from obj where ra between 2 and 5 and decl between 1 and 10;"
    test(quer)
    tokens = getTokens(quer)
    # print "aslist", tokens.asList()
    # print "where", tokens.where
    # print "wherecond", tokens.where.asList()
    pass


    
