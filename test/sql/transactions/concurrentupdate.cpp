#include "catch.hpp"
#include "test_helpers.hpp"

#include <random>
#include <thread>

using namespace duckdb;
using namespace std;

#define TRANSACTION_UPDATE_COUNT 1000
#define TOTAL_ACCOUNTS 20
#define MONEY_PER_ACCOUNT 10

TEST_CASE("Single thread update", "[transactions]") {
	unique_ptr<DuckDBResult> result;
	DuckDB db(nullptr);
	DuckDBConnection con(db);
	std::vector<std::unique_ptr<DuckDBConnection>> connections;

	// initialize the database
	con.Query("CREATE TABLE integers(i INTEGER);");
	int sum = 0;
	for (size_t i = 0; i < TOTAL_ACCOUNTS; i++) {
		for (size_t j = 0; j < 10; j++) {
			con.Query("INSERT INTO integers VALUES (" + to_string(j + 1) +
			          ");");
			sum += j + 1;
		}
	}

	// check the sum
	result = con.Query("SELECT SUM(i) FROM integers");
	CHECK_COLUMN(result, 0, {sum});

	// simple update, we should update INSERT_ELEMENTS elements
	result = con.Query("UPDATE integers SET i=4 WHERE i=2");
	CHECK_COLUMN(result, 0, {TOTAL_ACCOUNTS});

	// check updated sum
	result = con.Query("SELECT SUM(i) FROM integers");
	CHECK_COLUMN(result, 0, {sum + 2 * TOTAL_ACCOUNTS});
}

static volatile bool finished_updating = false;
static void read_total_balance(DuckDB *db) {
	REQUIRE(db);
	DuckDBConnection con(*db);
	while (!finished_updating) {
		// the total balance should remain constant regardless of updates
		auto result = con.Query("SELECT SUM(money) FROM accounts");
		CHECK_COLUMN(result, 0, {TOTAL_ACCOUNTS * MONEY_PER_ACCOUNT});
	}
}

// static void read_initial_table(DuckDB *db) {
// 	REQUIRE(db);
// 	DuckDBConnection con(*db);
// 	con.Query("BEGIN TRANSACTION");
// 	auto initial_result = con.Query("SELECT * FROM accounts ORDER BY id");
// 	std::vector<duckdb::Value> columns[2];
// 	for(size_t j = 0; j < 2; j++) {
// 		for(size_t i = 0; i < initial_result->collection.count; i++) {
// 			columns[j].push_back(initial_result->collection.GetValue(j, i));
// 		}
// 	}
// 	while (!finished_updating) {
// 		// the table should remain the same regardless of updates
// 		auto new_result = con.Query("SELECT * FROM accounts ORDER BY id");
// 		CHECK_COLUMN(new_result, 0, columns[0]);
// 		CHECK_COLUMN(new_result, 1, columns[1]);
// 	}
// 	con.Query("COMMIT");
// }

TEST_CASE("[SLOW] Concurrent update", "[updates]") {
	unique_ptr<DuckDBResult> result;
	DuckDB db(nullptr);
	DuckDBConnection con(db);

	// fixed seed random numbers
	mt19937 generator;
	generator.seed(42);
	uniform_int_distribution<int> account_distribution(0, TOTAL_ACCOUNTS - 1);
	auto random_account = bind(account_distribution, generator);

	uniform_int_distribution<int> amount_distribution(0, MONEY_PER_ACCOUNT);
	auto random_amount = bind(amount_distribution, generator);

	finished_updating = false;
	// initialize the database
	con.Query("CREATE TABLE accounts(id INTEGER, money INTEGER)");
	for (size_t i = 0; i < TOTAL_ACCOUNTS; i++) {
		con.Query("INSERT INTO accounts VALUES (" + to_string(i) + ", " +
		          to_string(MONEY_PER_ACCOUNT) + ");");
	}

	// launch separate thread for reading aggregate
	thread read_thread(read_total_balance, &db);

	// start vigorously updating balances in this thread
	for (size_t i = 0; i < TRANSACTION_UPDATE_COUNT; i++) {
		int from = random_account();
		int to = random_account();
		while (to == from) {
			to = random_account();
		}
		int amount = random_amount();

		REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
		result =
		    con.Query("SELECT money FROM accounts WHERE id=" + to_string(from));
		Value money_from = result->collection.GetValue(0, 0);
		result =
		    con.Query("SELECT money FROM accounts WHERE id=" + to_string(to));
		Value money_to = result->collection.GetValue(0, 0);

		REQUIRE_NO_FAIL(con.Query("UPDATE accounts SET money = money - " +
		                          to_string(amount) +
		                          " WHERE id = " + to_string(from)));
		REQUIRE_NO_FAIL(con.Query("UPDATE accounts SET money = money + " +
		                          to_string(amount) +
		                          " WHERE id = " + to_string(to)));

		result =
		    con.Query("SELECT money FROM accounts WHERE id=" + to_string(from));
		Value new_money_from = result->collection.GetValue(0, 0);
		result =
		    con.Query("SELECT money FROM accounts WHERE id=" + to_string(to));
		Value new_money_to = result->collection.GetValue(0, 0);

		Value expected_money_from, expected_money_to;

		Value::Subtract(money_from, Value::INTEGER(amount),
		                expected_money_from);
		Value::Add(money_to, Value::INTEGER(amount), expected_money_to);

		REQUIRE(Value::Equals(new_money_from, expected_money_from));
		REQUIRE(Value::Equals(new_money_to, expected_money_to));

		REQUIRE_NO_FAIL(con.Query("COMMIT"));
	}
	finished_updating = true;
	read_thread.join();
}

static std::atomic<size_t> finished_threads;

static void write_random_numbers_to_account(DuckDB *db, size_t nr) {
	REQUIRE(db);
	DuckDBConnection con(*db);
	for (size_t i = 0; i < TRANSACTION_UPDATE_COUNT; i++) {
		// just make some changes to the total
		// the total amount of money after the commit is the same
		REQUIRE_NO_FAIL(con.Query("BEGIN TRANSACTION"));
		REQUIRE_NO_FAIL(con.Query("UPDATE accounts SET money = money + " +
		                          to_string(i * 2) +
		                          " WHERE id = " + to_string(nr)));
		REQUIRE_NO_FAIL(con.Query("UPDATE accounts SET money = money - " +
		                          to_string(i) +
		                          " WHERE id = " + to_string(nr)));
		REQUIRE_NO_FAIL(con.Query("UPDATE accounts SET money = money - " +
		                          to_string(i * 2) +
		                          " WHERE id = " + to_string(nr)));
		REQUIRE_NO_FAIL(con.Query("UPDATE accounts SET money = money + " +
		                          to_string(i) +
		                          " WHERE id = " + to_string(nr)));
		REQUIRE_NO_FAIL(con.Query("COMMIT"));
	}
	finished_threads++;
	if (finished_threads == TOTAL_ACCOUNTS) {
		finished_updating = true;
	}
}

TEST_CASE("[SLOW] Multiple concurrent updaters", "[updates]") {
	unique_ptr<DuckDBResult> result;
	DuckDB db(nullptr);
	DuckDBConnection con(db);

	finished_updating = false;
	finished_threads = 0;
	// initialize the database
	con.Query("CREATE TABLE accounts(id INTEGER, money INTEGER)");
	for (size_t i = 0; i < TOTAL_ACCOUNTS; i++) {
		con.Query("INSERT INTO accounts VALUES (" + to_string(i) + ", " +
		          to_string(MONEY_PER_ACCOUNT) + ");");
	}

	std::thread write_threads[TOTAL_ACCOUNTS];
	// launch a thread for reading the table
	thread read_thread(read_total_balance, &db);
	// launch several threads for updating the table
	for (size_t i = 0; i < TOTAL_ACCOUNTS; i++) {
		write_threads[i] = thread(write_random_numbers_to_account, &db, i);
	}
	read_thread.join();
	for (size_t i = 0; i < TOTAL_ACCOUNTS; i++) {
		write_threads[i].join();
	}
}