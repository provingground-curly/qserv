// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2013-2015 AURA/LSST.
 *
 * This product includes software developed by the
 * LSST Project (http://www.lsst.org/).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the LSST License Statement and
 * the GNU General Public License along with this program.  If not,
 * see <http://www.lsstcorp.org/LegalNotices/>.
 */
/**
  * @file
  *
  * @brief Implementation of a SelectParser
  *
  *  SelectParser is the top-level manager for everything attached to
  *  parsing the top-level SQL query. Given an input query and a
  *  configuration, computes a query info structure, name ref list,
  *  and a "query plan".
  *
  * @author Daniel L. Wang, SLAC
  */

// Class header
#include "parser/SelectParser.h"

// System headers
#include <cstdio>
#include <functional>
#include <strings.h>
#include <vector>

// Third-party headers
#include <antlr/CommonAST.hpp>

#include "boost/bind.hpp"

// Qserv headers
#include "global/Bug.h"
#include "parser/ParseException.h"
#include "parser/parseTreeUtil.h"
#include "parser/SelectFactory.h"
#include "parser/SqlSQL2Parser.hpp"   //!!! Order is important, SqlSQL2Parser must be first
#include "parser/SqlSQL2Lexer.hpp"
#include "parser/SqlSQL2TokenTypes.hpp"
#include "query/SelectStmt.h"
#include "util/IterableFormatter.h"

// these must be included before Log.h because they have a function called LOGS
// that conflicts with the LOGS macro defined in Log.h
#include "antlr4-runtime.h"
#include "parser/QSMySqlLexer.h"
#include "parser/QSMySqlParser.h"
#include "parser/QSMySqlListener.h"

// must come after QSMySqlLexer & Parser because of namespace collision
#include "lsst/log/Log.h"

// ANTLR headers, declare after auto-generated headers to
// reduce namespace pollution
#include <antlr/MismatchedCharException.hpp>
#include <antlr/MismatchedTokenException.hpp>
#include <antlr/NoViableAltException.hpp>
#include <antlr/NoViableAltForCharException.hpp>
#include <antlr/RecognitionException.hpp>
#include <antlr/SemanticException.hpp>

namespace {
LOG_LOGGER _log = LOG_GET("lsst.qserv.ccontrol.SelectParser");

// For the current query, this returns a list where each pair contains a bit of the string from the query
// and how antlr4 tokenized that bit of string. It is useful for debugging problems where antlr4 did not
// parse a query as expected, in the case where the string was not tokenized as expected.
typedef std::vector<std::pair<std::string, std::string>> VecPairStr;
VecPairStr getTokenPairs(antlr4::CommonTokenStream & tokens, QSMySqlLexer & lexer) {
    VecPairStr ret;
    for (auto&& t : tokens.getTokens()) {
        std::string name = lexer.getVocabulary().getSymbolicName(t->getType());
        if (name.empty()) {
            name = lexer.getVocabulary().getLiteralName(t->getType());
        }
        ret.push_back(make_pair(std::move(name), t->getText()));
    }
    return ret;
}

}

namespace lsst {
namespace qserv {
namespace parser {

////////////////////////////////////////////////////////////////////////
// AntlrParser -- Antlr parsing complex
////////////////////////////////////////////////////////////////////////
class AntlrParser {
public:
    virtual ~AntlrParser() {}
    virtual void setup() = 0;
    virtual void run() = 0;
    virtual query::SelectStmt::Ptr getStatement() = 0;
};

class Antlr2Parser : public AntlrParser {
public:
    Antlr2Parser(std::string const& q)
        : statement(q)
        , stream(q, std::stringstream::in | std::stringstream::out)
        , lexer(stream)
        , parser(lexer)
    {}

    void setup() override {
        sf.attachTo(parser);
    }

    void run() override {
        parser.initializeASTFactory(factory);
        parser.setASTFactory(&factory);
        try {
            parser.qserv_stmt();
        } catch(antlr::NoViableAltException& e) {
            throw ParseException("Parse error(ANTLR):"
                                 + e.getMessage(), e.node);
        } catch(antlr::NoViableAltForCharException& e) {
            throw ParseException("Parse error(unexpected char lex):"
                                 + e.getMessage(), e);
        } catch(antlr::MismatchedCharException& e) {
            throw ParseException("Parse char mismatch error:"
                                 + e.getMessage(), e);
        } catch(antlr::MismatchedTokenException& e) {
            throw ParseException("Parse token mismatch error:"
                                 + e.getMessage(), e.node);
        } catch(antlr::SemanticException& e) {
            throw ParseException("Parse error(corrupted, semantic):"
                                 + e.getMessage(), e);
        } catch(antlr::RecognitionException& e) {
            throw ParseException("Parse error(corrupted, recognition):"
                                 + e.getMessage(), e);
        } catch(antlr::ANTLRException& e) {
            throw ParseException("Unknown ANTLR error:" + e.getMessage(), e);
        } catch(ParseException& e) {
            throw;
        } catch (Bug& b) {
            throw ParseException(b);
        } catch (std::logic_error& e) {
            throw Bug(e.what());
        } catch (...) {
            // leading underscores in literals as value_expr throw this.
            throw Bug("Unknown parsing error");
        }
        RefAST a = parser.getAST();
    }

    query::SelectStmt::Ptr getStatement() override {
        return sf.getStatement();
        // _selectStmt->diagnose(); // helpful for debugging.
    }

private:
    SelectFactory sf;
    std::string statement;
    std::stringstream stream;
    ASTFactory factory;
    SqlSQL2Lexer lexer;
    SqlSQL2Parser parser;
};


class Antlr4Parser : public AntlrParser {
public:

    static std::shared_ptr<Antlr4Parser> create(std::string const & q) {
        return std::shared_ptr<Antlr4Parser>(new Antlr4Parser(q));
    }

    void setup() override {
        // no op
    }

    void run() override {
        using namespace antlr4;
        ANTLRInputStream input(statement);
        QSMySqlLexer lexer(&input);
        CommonTokenStream tokens(&lexer);
        tokens.fill();
        LOGS(_log, LOG_LVL_TRACE, "New user query, antlr4 tokens: " <<  util::printable(getTokenPairs(tokens, lexer)));
        QSMySqlParser parser(&tokens);
        tree::ParseTree *tree = parser.root();
        LOGS(_log, LOG_LVL_TRACE, "New user query, antlr4 string tree: " << tree->toStringTree(&parser));
        tree::ParseTreeWalker walker;
        walker.walk(&listener, tree);
    }

    query::SelectStmt::Ptr getStatement() override {
        return listener.getSelectStatement();
    }

private:
    Antlr4Parser(std::string const& q)
        : _statement(q)
    {}

    std::string statement;
    parser::QSMySqlListener listener;
};


////////////////////////////////////////////////////////////////////////
// class SelectParser
////////////////////////////////////////////////////////////////////////


SelectParser::Ptr SelectParser::newInstance(std::string const& statement, AntlrVersion v) {
    return std::shared_ptr<SelectParser>(new SelectParser(statement, v));
}


SelectParser::SelectParser(std::string const& statement, AntlrVersion v)
    :_statement(statement) {
    if (ANTLR2 == v) {
        _aParser = std::make_shared<Antlr2Parser>(_statement);
        return;
    }

    _aParser = Antlr4Parser::create(_statement);
}


void SelectParser::setup() {
    _aParser->setup();
    _aParser->run();
    _selectStmt = _aParser->getStatement();
}

}}} // namespace lsst::qserv::parser
