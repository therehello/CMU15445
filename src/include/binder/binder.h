//===----------------------------------------------------------------------===//
//                         BusTub
//
// bustub/binder/parser.h
//
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Copyright 2018-2022 Stichting DuckDB Foundation
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice (including the next paragraph)
// shall be included in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <string>

#include "binder/simplified_token.h"
#include "binder/tokens.h"
#include "catalog/catalog.h"
#include "catalog/column.h"
#include "common/macros.h"
#include "common/util/string_util.h"
#include "fmt/format.h"
#include "nodes/parsenodes.hpp"
#include "nodes/pg_list.hpp"
#include "pg_definitions.hpp"
#include "postgres_parser.hpp"
#include "type/type_id.h"
#include "type/value.h"

namespace duckdb_libpgquery {
struct PGList;
struct PGSelectStmt;
struct PGAConst;
struct PGAStar;
struct PGFuncCall;
struct PGNode;
struct PGColumnRef;
struct PGResTarget;
struct PGAExpr;
struct PGJoinExpr;
}  // namespace duckdb_libpgquery

namespace bustub {

class Catalog;
class BoundExpression;
class BoundTableRef;
class BoundExpression;
class BoundExpressionListRef;
class BoundOrderBy;
class SelectStatement;
class CreateStatement;
class ExplainStatement;

/**
 * The binder is responsible for transforming the Postgres parse tree to a binder tree
 * which can be recognized unambiguously by the BusTub planner.
 */
class Binder {
 public:
  explicit Binder(const Catalog &catalog);

  /** The parsed SQL statements from an invocation to ParseQuery. */
  std::vector<std::unique_ptr<BoundStatement>> statements_;

  /** Attempts to parse a query into a series of SQL statements. The parsed statements
   * will be stored in the `statements_` variable.
   */
  void ParseAndBindQuery(const std::string &query);

  /** Return true if the given text matches a keyword of the parser. */
  static auto IsKeyword(const std::string &text) -> bool;

  /** Return a list of all keywords in the parser. */
  static auto KeywordList() -> std::vector<ParserKeyword>;

  /** Tokenize a query, returning the raw tokens together with their locations. */
  static auto Tokenize(const std::string &query) -> std::vector<SimplifiedToken>;

  /** Transform a Postgres parse tree into a std::vector of SQL Statements. */
  auto TransformParseTree(duckdb_libpgquery::PGList *tree) -> std::vector<std::unique_ptr<BoundStatement>>;

  /** Transform a Postgres statement into a single SQL statement. */
  auto TransformStatement(duckdb_libpgquery::PGNode *stmt) -> std::unique_ptr<BoundStatement>;

  /** Get the std::string representation of a Postgres node tag. */
  static auto NodeTagToString(duckdb_libpgquery::PGNodeTag type) -> std::string;

  // The following parts are undocumented. One `BindXXX` functions simply corresponds to a
  // node type in the Postgres parse tree.

  auto BindExplain(duckdb_libpgquery::PGExplainStmt *stmt) -> std::unique_ptr<ExplainStatement>;

  auto BindCreate(duckdb_libpgquery::PGCreateStmt *pg_stmt) -> std::unique_ptr<CreateStatement>;

  auto BindColumnDefinition(duckdb_libpgquery::PGColumnDef *cdef) -> Column;

  auto BindSelect(duckdb_libpgquery::PGSelectStmt *pg_stmt) -> std::unique_ptr<SelectStatement>;

  auto BindSelectList(duckdb_libpgquery::PGList *list) -> std::vector<std::unique_ptr<BoundExpression>>;

  auto BindWhere(duckdb_libpgquery::PGNode *root) -> std::unique_ptr<BoundExpression>;

  auto BindGroupBy(duckdb_libpgquery::PGList *list) -> std::vector<std::unique_ptr<BoundExpression>>;

  auto BindHaving(duckdb_libpgquery::PGNode *root) -> std::unique_ptr<BoundExpression>;

  auto BindExpression(duckdb_libpgquery::PGNode *node) -> std::unique_ptr<BoundExpression>;

  auto BindExpressionList(duckdb_libpgquery::PGList *list) -> std::vector<std::unique_ptr<BoundExpression>>;

  auto BindConstant(duckdb_libpgquery::PGAConst *node) -> std::unique_ptr<BoundExpression>;

  auto BindColumnRef(duckdb_libpgquery::PGColumnRef *node) -> std::unique_ptr<BoundExpression>;

  auto BindResTarget(duckdb_libpgquery::PGResTarget *root) -> std::unique_ptr<BoundExpression>;

  auto BindStar(duckdb_libpgquery::PGAStar *node) -> std::unique_ptr<BoundExpression>;

  auto BindFuncCall(duckdb_libpgquery::PGFuncCall *root) -> std::unique_ptr<BoundExpression>;

  auto BindAExpr(duckdb_libpgquery::PGAExpr *root) -> std::unique_ptr<BoundExpression>;

  auto BindBoolExpr(duckdb_libpgquery::PGBoolExpr *root) -> std::unique_ptr<BoundExpression>;

  auto BindFrom(duckdb_libpgquery::PGList *list) -> std::unique_ptr<BoundTableRef>;

  auto BindTableRef(duckdb_libpgquery::PGNode *node) -> std::unique_ptr<BoundTableRef>;

  auto BindJoin(duckdb_libpgquery::PGJoinExpr *root) -> std::unique_ptr<BoundTableRef>;

  auto GetAllColumns(const BoundTableRef &scope) -> std::vector<std::unique_ptr<BoundExpression>>;

  auto ResolveColumn(const BoundTableRef &scope, const std::vector<std::string> &col_name)
      -> std::unique_ptr<BoundExpression>;

  auto BindInsert(duckdb_libpgquery::PGInsertStmt *pg_stmt) -> std::unique_ptr<InsertStatement>;

  auto BindValuesList(duckdb_libpgquery::PGList *list) -> std::unique_ptr<BoundExpressionListRef>;

  auto BindLimitCount(duckdb_libpgquery::PGNode *root) -> std::unique_ptr<BoundExpression>;

  auto BindLimitOffset(duckdb_libpgquery::PGNode *root) -> std::unique_ptr<BoundExpression>;

  auto BindSort(duckdb_libpgquery::PGList *list) -> std::vector<std::unique_ptr<BoundOrderBy>>;

  class ContextGuard {
   public:
    explicit ContextGuard(const BoundTableRef **scope) {
      old_scope_ = *scope;
      scope_ptr_ = scope;
      *scope = nullptr;
    }
    ~ContextGuard() { *scope_ptr_ = old_scope_; }
    DISALLOW_COPY_AND_MOVE(ContextGuard);

   private:
    const BoundTableRef *old_scope_;
    const BoundTableRef **scope_ptr_;
  };

  /** If any function needs to modify the scope, it MUST hold the context guard, so that
   * the context will be recovered after the function returns. Currently, it's used in
   * `BindFrom` and `BindJoin`.
   */
  auto NewContext() -> ContextGuard { return ContextGuard(&scope_); }

 private:
  /** Catalog will be used during the binding process. USERS SHOULD ENSURE IT OUTLIVES THE BINDER,
   * otherwise it's a dangling reference.
   */
  const Catalog &catalog_;

  /** The current scope for resolving column ref, used in binding expressions */
  const BoundTableRef *scope_;
};

}  // namespace bustub
