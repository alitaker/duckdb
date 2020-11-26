#include "duckdb/parser/parsed_expression_iterator.hpp"

#include "duckdb/parser/expression/list.hpp"

namespace duckdb {
using namespace std;

void ParsedExpressionIterator::EnumerateChildren(const ParsedExpression &expression,
                                                 function<void(const ParsedExpression &child)> callback) {
	EnumerateChildren((ParsedExpression &)expression, [&](unique_ptr<ParsedExpression> &child) { callback(*child); });
}

void ParsedExpressionIterator::EnumerateChildren(ParsedExpression &expr,
                                                 function<void(ParsedExpression &child)> callback) {
	EnumerateChildren(expr, [&](unique_ptr<ParsedExpression> &child) { callback(*child); });
}

void ParsedExpressionIterator::EnumerateChildren(ParsedExpression &expr,
                                                 function<void(unique_ptr<ParsedExpression> &child)> callback) {
	switch (expr.expression_class) {
	case ExpressionClass::CASE: {
		auto &case_expr = (CaseExpression &)expr;
		callback(case_expr.check);
		callback(case_expr.result_if_true);
		callback(case_expr.result_if_false);
		break;
	}
	case ExpressionClass::CAST: {
		auto &cast_expr = (CastExpression &)expr;
		callback(cast_expr.child);
		break;
	}
	case ExpressionClass::COLLATE: {
		auto &cast_expr = (CollateExpression &)expr;
		callback(cast_expr.child);
		break;
	}
	case ExpressionClass::COMPARISON: {
		auto &comp_expr = (ComparisonExpression &)expr;
		callback(comp_expr.left);
		callback(comp_expr.right);
		break;
	}
	case ExpressionClass::CONJUNCTION: {
		auto &conj_expr = (ConjunctionExpression &)expr;
		for (auto &child : conj_expr.children) {
			callback(child);
		}
		break;
	}
	case ExpressionClass::FUNCTION: {
		auto &func_expr = (FunctionExpression &)expr;
		for (auto &child : func_expr.children) {
			callback(child);
		}
		break;
	}
	case ExpressionClass::OPERATOR: {
		auto &op_expr = (OperatorExpression &)expr;
		for (auto &child : op_expr.children) {
			callback(child);
		}
		break;
	}
	case ExpressionClass::SUBQUERY: {
		auto &subquery_expr = (SubqueryExpression &)expr;
		if (subquery_expr.child) {
			callback(subquery_expr.child);
		}
		break;
	}
	case ExpressionClass::WINDOW: {
		auto &window_expr = (WindowExpression &)expr;
		for (auto &partition : window_expr.partitions) {
			callback(partition);
		}
		for (auto &order : window_expr.orders) {
			callback(order.expression);
		}
		for (auto &child : window_expr.children) {
			callback(child);
		}
		if (window_expr.offset_expr) {
			callback(window_expr.offset_expr);
		}
		if (window_expr.default_expr) {
			callback(window_expr.default_expr);
		}
		break;
	}
	case ExpressionClass::BOUND_EXPRESSION:
	case ExpressionClass::COLUMN_REF:
	case ExpressionClass::CONSTANT:
	case ExpressionClass::DEFAULT:
	case ExpressionClass::STAR:
	case ExpressionClass::TABLE_STAR:
	case ExpressionClass::PARAMETER:
		// these node types have no children
		break;
	default:
		// called on non ParsedExpression type!
		throw NotImplementedException("Unimplemented expression class");
	}
}

} // namespace duckdb
