#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <stdexcept>

#include <cgicc/Cgicc.h>
#include <cgicc/HTTPPlainHeader.h>
#include <libpq-fe.h>

using namespace std;
using namespace cgicc;

// ===================== НАСТРОЙКИ ПОДКЛЮЧЕНИЯ К Postgres =====================

const char* CONNINFO = "dbname=bankdb user=bank_user password=Dima1234 host=localhost port=5432";

// ===================== ВСПОМОГАТЕЛЬНЫЕ СТРУКТУРЫ (НЕ БД, ПРОСТО ДЛЯ УДОБСТВА) =====================

struct Account {
    string number;
    double balance;
};

struct User {
    int id;
    string fullName;
    string email;
    string password;
};

// ===================== RAII-ОБЁРТКА ДЛЯ СОЕДИНЕНИЯ С БД =====================

struct PgConn {
    PGconn* conn;

    PgConn() {
        conn = PQconnectdb(CONNINFO);
        if (PQstatus(conn) != CONNECTION_OK) {
            string msg = PQerrorMessage(conn);
            PQfinish(conn);
            throw runtime_error("Ошибка подключения к БД: " + msg);
        }
    }

    ~PgConn() {
        if (conn) {
            PQfinish(conn);
        }
    }
};

// ===================== ВСПОМОГАТЕЛЬНЫЕ ШТУКИ =====================

void printJsonHeader() {
    cout << "Content-type: application/json\n\n";
}

void jsonError(const string& msg) {
    printJsonHeader();
    cout << "{ \"success\": false, \"message\": \"" << msg << "\" }";
}

void jsonOkMessage(const string& msg) {
    printJsonHeader();
    cout << "{ \"success\": true, \"message\": \"" << msg << "\" }";
}

bool parseIntSafe(const string& s, int& out) {
    try {
        size_t idx;
        int v = stoi(s, &idx);
        if (idx != s.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseDoubleSafe(const string& s, double& out) {
    try {
        size_t idx;
        double v = stod(s, &idx);
        if (idx != s.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

string getParam(Cgicc& cgi, const string& name, bool& present) {
    form_iterator it = cgi.getElement(name);
    if (it == cgi.getElements().end()) {
        present = false;
        return "";
    }
    present = true;
    return it->getValue();
}

bool isAllDigits(const string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!isdigit((unsigned char)c)) return false;
    }
    return true;
}

// генерация 16-значного номера счётa
string generateAccountNumber() {
    string num = "4000";
    for (int i = 0; i < 12; ++i) {
        int d = rand() % 10;
        num.push_back('0' + d);
    }
    return num;
}

// ===================== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ РАБОТЫ С БД =====================

// Проверка, существует ли пользователь по id
bool dbUserExists(PGconn* conn, int userId) {
    const char* params[1];
    string userIdStr = to_string(userId);
    params[0] = userIdStr.c_str();

    PGresult* res = PQexecParams(
        conn,
        "SELECT 1 FROM users WHERE id = $1",
        1,
        nullptr,
        params,
        nullptr,
        nullptr,
        0
    );

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        PQclear(res);
        throw runtime_error("Ошибка запроса к БД (dbUserExists)");
    }

    bool exists = (PQntuples(res) > 0);
    PQclear(res);
    return exists;
}

// Получить пользователя по логину (id или email)
bool dbFindUserByLogin(PGconn* conn, const string& login, User& outUser) {
    PGresult* res = nullptr;
    const char* params[1];

    if (isAllDigits(login)) {
        string idStr = login;
        params[0] = idStr.c_str();

        res = PQexecParams(
            conn,
            "SELECT id, full_name, email, password FROM users WHERE id = $1::int",
            1,
            nullptr,
            params,
            nullptr,
            nullptr,
            0
        );
    } else {
        params[0] = login.c_str();
        res = PQexecParams(
            conn,
            "SELECT id, full_name, email, password FROM users WHERE email = $1",
            1,
            nullptr,
            params,
            nullptr,
            nullptr,
            0
        );
    }

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        PQclear(res);
        throw runtime_error("Ошибка запроса к БД (dbFindUserByLogin)");
    }

    if (PQntuples(res) == 0) {
        PQclear(res);
        return false;
    }

    outUser.id       = stoi(PQgetvalue(res, 0, 0));
    outUser.fullName = PQgetvalue(res, 0, 1);
    outUser.email    = PQgetvalue(res, 0, 2);
    outUser.password = PQgetvalue(res, 0, 3);

    PQclear(res);
    return true;
}

// Получить все счета пользователя
vector<Account> dbGetAccounts(PGconn* conn, int userId) {
    const char* params[1];
    string userIdStr = to_string(userId);
    params[0] = userIdStr.c_str();

    PGresult* res = PQexecParams(
        conn,
        "SELECT number, balance FROM accounts WHERE user_id = $1 ORDER BY id",
        1,
        nullptr,
        params,
        nullptr,
        nullptr,
        0
    );

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        PQclear(res);
        throw runtime_error("Ошибка запроса к БД (dbGetAccounts)");
    }

    vector<Account> result;
    int rows = PQntuples(res);
    for (int i = 0; i < rows; ++i) {
        Account a;
        a.number  = PQgetvalue(res, i, 0);
        a.balance = stod(PQgetvalue(res, i, 1));
        result.push_back(a);
    }

    PQclear(res);
    return result;
}

// Подсчитать количество счетов пользователя
int dbCountAccounts(PGconn* conn, int userId) {
    const char* params[1];
    string userIdStr = to_string(userId);
    params[0] = userIdStr.c_str();

    PGresult* res = PQexecParams(
        conn,
        "SELECT COUNT(*) FROM accounts WHERE user_id = $1",
        1,
        nullptr,
        params,
        nullptr,
        nullptr,
        0
    );

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        PQclear(res);
        throw runtime_error("Ошибка запроса к БД (dbCountAccounts)");
    }

    int count = stoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    return count;
}

// Найти баланс счёта по номеру (если нет — возвращаем false)
bool dbGetAccountBalance(PGconn* conn, const string& accNumber, double& balanceOut) {
    const char* params[1];
    params[0] = accNumber.c_str();

    PGresult* res = PQexecParams(
        conn,
        "SELECT balance FROM accounts WHERE number = $1",
        1,
        nullptr,
        params,
        nullptr,
        nullptr,
        0
    );

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        PQclear(res);
        throw runtime_error("Ошибка запроса к БД (dbGetAccountBalance)");
    }

    if (PQntuples(res) == 0) {
        PQclear(res);
        return false;
    }

    balanceOut = stod(PQgetvalue(res, 0, 0));
    PQclear(res);
    return true;
}

// ===================== HANDLERS =====================

// REGISTER
void handleRegister(Cgicc& cgi) {
    bool pFull, pEmail, pPwd;
    string fullName = getParam(cgi, "fullName", pFull);
    string email    = getParam(cgi, "email",    pEmail);
    string password = getParam(cgi, "password", pPwd);

    if (!pFull || !pEmail || !pPwd || fullName.empty() || email.empty() || password.size() < 6) {
        jsonError("Некорректные данные регистрации.");
        return;
    }

    try {
        PgConn db;

        // Проверка уникальности email
        const char* params[1];
        params[0] = email.c_str();
        PGresult* res = PQexecParams(
            db.conn,
            "SELECT id FROM users WHERE email = $1",
            1,
            nullptr,
            params,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            PQclear(res);
            throw runtime_error("Ошибка запроса к БД (register: check email)");
        }

        if (PQntuples(res) > 0) {
            PQclear(res);
            jsonError("Пользователь с таким email уже существует.");
            return;
        }
        PQclear(res);

        // Вставка пользователя
        const char* params2[3];
        params2[0] = fullName.c_str();
        params2[1] = email.c_str();
        params2[2] = password.c_str();

        res = PQexecParams(
            db.conn,
            "INSERT INTO users(full_name, email, password) "
            "VALUES ($1, $2, $3) RETURNING id",
            3,
            nullptr,
            params2,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
            PQclear(res);
            throw runtime_error("Ошибка вставки пользователя.");
        }

        int newId = stoi(PQgetvalue(res, 0, 0));
        PQclear(res);

        printJsonHeader();
        cout << "{ \"success\": true, "
             << "\"message\": \"Регистрация выполнена.\", "
             << "\"userId\": " << newId << ", "
             << "\"fullName\": \"" << fullName << "\" }";

    } catch (const exception& e) {
        jsonError(string("Внутренняя ошибка (register): ") + e.what());
    }
}

// LOGIN
void handleLogin(Cgicc& cgi) {
    bool pLogin, pPwd;
    string login = getParam(cgi, "login", pLogin);
    string password = getParam(cgi, "password", pPwd);

    if (!pLogin || !pPwd || login.empty() || password.empty()) {
        jsonError("Введите логин и пароль.");
        return;
    }

    try {
        PgConn db;
        User u;
        if (!dbFindUserByLogin(db.conn, login, u) || u.password != password) {
            jsonError("Неверный логин или пароль.");
            return;
        }

        printJsonHeader();
        cout << "{ \"success\": true, "
             << "\"message\": \"Вход выполнен.\", "
             << "\"userId\": " << u.id << ", "
             << "\"fullName\": \"" << u.fullName << "\" }";

    } catch (const exception& e) {
        jsonError(string("Внутренняя ошибка (login): ") + e.what());
    }
}

// GET ACCOUNTS
void handleGetAccounts(Cgicc& cgi) {
    bool pUid;
    string sUserId = getParam(cgi, "userId", pUid);
    if (!pUid || sUserId.empty()) {
        jsonError("Не указан userId.");
        return;
    }

    int userId = 0;
    if (!parseIntSafe(sUserId, userId)) {
        jsonError("Некорректный userId.");
        return;
    }

    try {
        PgConn db;

        if (!dbUserExists(db.conn, userId)) {
            jsonError("Пользователь не найден.");
            return;
        }

        auto accounts = dbGetAccounts(db.conn, userId);

        printJsonHeader();
        cout << "{ \"success\": true, \"accounts\": [";

        for (size_t i = 0; i < accounts.size(); ++i) {
            if (i > 0) cout << ", ";
            cout << "{ \"number\": \"" << accounts[i].number << "\", "
                 << "\"balance\": " << accounts[i].balance << " }";
        }

        cout << "] }";

    } catch (const exception& e) {
        jsonError(string("Внутренняя ошибка (getAccounts): ") + e.what());
    }
}

// CREATE ACCOUNT
void handleCreateAccount(Cgicc& cgi) {
    bool pUid;
    string sUserId = getParam(cgi, "userId", pUid);
    if (!pUid || sUserId.empty()) {
        jsonError("Не указан userId.");
        return;
    }

    int userId = 0;
    if (!parseIntSafe(sUserId, userId)) {
        jsonError("Некорректный userId.");
        return;
    }

    try {
        PgConn db;

        if (!dbUserExists(db.conn, userId)) {
            jsonError("Пользователь не найден.");
            return;
        }

        int cnt = dbCountAccounts(db.conn, userId);
        if (cnt >= 3) {
            jsonError("Нельзя создать больше 3 счетов.");
            return;
        }

        // Генерим номер и вставляем. На всякий случай можно проверить на уникальность.
        string accNumber;
        while (true) {
            accNumber = generateAccountNumber();
            const char* paramsCheck[1];
            paramsCheck[0] = accNumber.c_str();
            PGresult* resCheck = PQexecParams(
                db.conn,
                "SELECT 1 FROM accounts WHERE number = $1",
                1,
                nullptr,
                paramsCheck,
                nullptr,
                nullptr,
                0
            );
            if (PQresultStatus(resCheck) != PGRES_TUPLES_OK) {
                PQclear(resCheck);
                throw runtime_error("Ошибка проверки номера счета.");
            }
            bool exists = (PQntuples(resCheck) > 0);
            PQclear(resCheck);
            if (!exists) break; // нашли уникальный
        }

        const char* params[2];
        string userIdStr = to_string(userId);
        params[0] = userIdStr.c_str();
        params[1] = accNumber.c_str();

        PGresult* res = PQexecParams(
            db.conn,
            "INSERT INTO accounts(user_id, number, balance) VALUES ($1::int, $2, 0)",
            2,
            nullptr,
            params,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            PQclear(res);
            throw runtime_error("Ошибка вставки счета.");
        }
        PQclear(res);

        printJsonHeader();
        cout << "{ \"success\": true, "
             << "\"message\": \"Счёт создан.\", "
             << "\"accountNumber\": \"" << accNumber << "\" }";

    } catch (const exception& e) {
        jsonError(string("Внутренняя ошибка (createAccount): ") + e.what());
    }
}

// DELETE ACCOUNT
void handleDeleteAccount(Cgicc& cgi) {
    bool pUid, pAcc;
    string sUserId = getParam(cgi, "userId", pUid);
    string accNumber = getParam(cgi, "accountNumber", pAcc);

    if (!pUid || !pAcc || sUserId.empty() || accNumber.empty()) {
        jsonError("Не указан userId или accountNumber.");
        return;
    }

    int userId = 0;
    if (!parseIntSafe(sUserId, userId)) {
        jsonError("Некорректный userId.");
        return;
    }

    try {
        PgConn db;

        // Сначала узнаём баланс и принадлежность
        const char* params[2];
        string userIdStr = to_string(userId);
        params[0] = userIdStr.c_str();
        params[1] = accNumber.c_str();

        PGresult* res = PQexecParams(
            db.conn,
            "SELECT balance FROM accounts WHERE user_id = $1::int AND number = $2",
            2,
            nullptr,
            params,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            PQclear(res);
            throw runtime_error("Ошибка запроса к БД (deleteAccount: select)");
        }

        if (PQntuples(res) == 0) {
            PQclear(res);
            jsonError("Счёт не найден.");
            return;
        }

        double balance = stod(PQgetvalue(res, 0, 0));
        PQclear(res);

        if (balance > 0.0) {
            jsonError("Нельзя удалить счёт с ненулевым балансом.");
            return;
        }

        // Удаляем
        res = PQexecParams(
            db.conn,
            "DELETE FROM accounts WHERE user_id = $1::int AND number = $2",
            2,
            nullptr,
            params,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            PQclear(res);
            throw runtime_error("Ошибка удаления счета.");
        }

        PQclear(res);
        jsonOkMessage("Счёт удалён.");

    } catch (const exception& e) {
        jsonError(string("Внутренняя ошибка (deleteAccount): ") + e.what());
    }
}

// TOPUP
void handleTopup(Cgicc& cgi) {
    bool pAcc, pAmt;
    string accNumber = getParam(cgi, "accountNumber", pAcc);
    string sAmount   = getParam(cgi, "amount", pAmt);

    if (!pAcc || !pAmt || accNumber.empty() || sAmount.empty()) {
        jsonError("Не указан счёт или сумма.");
        return;
    }

    double amount = 0.0;
    if (!parseDoubleSafe(sAmount, amount) || amount <= 0.0) {
        jsonError("Сумма должна быть > 0.");
        return;
    }

    try {
        PgConn db;

        const char* params[2];
        params[0] = accNumber.c_str();
        string amountStr = to_string(amount);
        params[1] = amountStr.c_str();

        PGresult* res = PQexecParams(
            db.conn,
            "UPDATE accounts SET balance = balance + $2::double precision "
            "WHERE number = $1 "
            "RETURNING balance",
            2,
            nullptr,
            params,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            PQclear(res);
            throw runtime_error("Ошибка обновления баланса (topup).");
        }

        if (PQntuples(res) == 0) {
            PQclear(res);
            jsonError("Счёт не найден.");
            return;
        }

        double newBalance = stod(PQgetvalue(res, 0, 0));
        PQclear(res);

        printJsonHeader();
        cout << "{ \"success\": true, "
             << "\"message\": \"Баланс пополнен.\", "
             << "\"newBalance\": " << newBalance << " }";

    } catch (const exception& e) {
        jsonError(string("Внутренняя ошибка (topup): ") + e.what());
    }
}

// WITHDRAW
void handleWithdraw(Cgicc& cgi) {
    bool pAcc, pAmt;
    string accNumber = getParam(cgi, "accountNumber", pAcc);
    string sAmount   = getParam(cgi, "amount", pAmt);

    if (!pAcc || !pAmt || accNumber.empty() || sAmount.empty()) {
        jsonError("Не указан счёт или сумма.");
        return;
    }

    double amount = 0.0;
    if (!parseDoubleSafe(sAmount, amount) || amount <= 0.0) {
        jsonError("Сумма должна быть > 0.");
        return;
    }

    try {
        PgConn db;

        // Сначала узнаём баланс
        double balance = 0.0;
        if (!dbGetAccountBalance(db.conn, accNumber, balance)) {
            jsonError("Счёт не найден.");
            return;
        }

        if (balance < amount) {
            jsonError("Недостаточно средств.");
            return;
        }

        const char* params[2];
        params[0] = accNumber.c_str();
        string amountStr = to_string(amount);
        params[1] = amountStr.c_str();

        PGresult* res = PQexecParams(
            db.conn,
            "UPDATE accounts SET balance = balance - $2::double precision "
            "WHERE number = $1 "
            "RETURNING balance",
            2,
            nullptr,
            params,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            PQclear(res);
            throw runtime_error("Ошибка обновления баланса (withdraw).");
        }

        if (PQntuples(res) == 0) {
            PQclear(res);
            jsonError("Счёт не найден.");
            return;
        }

        double newBalance = stod(PQgetvalue(res, 0, 0));
        PQclear(res);

        printJsonHeader();
        cout << "{ \"success\": true, "
             << "\"message\": \"Снятие выполнено.\", "
             << "\"newBalance\": " << newBalance << " }";

    } catch (const exception& e) {
        jsonError(string("Внутренняя ошибка (withdraw): ") + e.what());
    }
}

// TRANSFER
void handleTransfer(Cgicc& cgi) {
    bool pFrom, pTo, pAmt;
    string fromAccNumber = getParam(cgi, "fromAccount", pFrom);
    string toAccNumber   = getParam(cgi, "toAccount",   pTo);
    string sAmount       = getParam(cgi, "amount",      pAmt);

    if (!pFrom || !pTo || !pAmt ||
        fromAccNumber.empty() || toAccNumber.empty() || sAmount.empty()) {
        jsonError("Нужно указать fromAccount, toAccount и amount.");
        return;
    }

    if (fromAccNumber == toAccNumber) {
        jsonError("Нельзя перевести на тот же счёт.");
        return;
    }

    double amount = 0.0;
    if (!parseDoubleSafe(sAmount, amount) || amount <= 0.0) {
        jsonError("Сумма должна быть > 0.");
        return;
    }

    try {
        PgConn db;

        // Транзакция
        PGresult* res = PQexec(db.conn, "BEGIN");
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            PQclear(res);
            throw runtime_error("Не удалось начать транзакцию.");
        }
        PQclear(res);

        // Блокируем обе записи (FOR UPDATE)
        const char* paramsFrom[1];
        paramsFrom[0] = fromAccNumber.c_str();
        res = PQexecParams(
            db.conn,
            "SELECT balance FROM accounts WHERE number = $1 FOR UPDATE",
            1,
            nullptr,
            paramsFrom,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            PQclear(res);
            PQexec(db.conn, "ROLLBACK");
            throw runtime_error("Ошибка выборки счета-отправителя.");
        }

        if (PQntuples(res) == 0) {
            PQclear(res);
            PQexec(db.conn, "ROLLBACK");
            jsonError("Счёт-отправитель не найден.");
            return;
        }

        double fromBalance = stod(PQgetvalue(res, 0, 0));
        PQclear(res);

        const char* paramsTo[1];
        paramsTo[0] = toAccNumber.c_str();
        res = PQexecParams(
            db.conn,
            "SELECT balance FROM accounts WHERE number = $1 FOR UPDATE",
            1,
            nullptr,
            paramsTo,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            PQclear(res);
            PQexec(db.conn, "ROLLBACK");
            throw runtime_error("Ошибка выборки счета-получателя.");
        }

        if (PQntuples(res) == 0) {
            PQclear(res);
            PQexec(db.conn, "ROLLBACK");
            jsonError("Счёт-получатель не найден.");
            return;
        }

        double toBalance = stod(PQgetvalue(res, 0, 0));
        PQclear(res);

        if (fromBalance < amount) {
            PQexec(db.conn, "ROLLBACK");
            jsonError("Недостаточно средств.");
            return;
        }

        // Обновляем оба счета
        const char* paramsUpdateFrom[2];
        const char* paramsUpdateTo[2];
        string amountStr = to_string(amount);
        paramsUpdateFrom[0] = amountStr.c_str();
        paramsUpdateFrom[1] = fromAccNumber.c_str();

        paramsUpdateTo[0] = amountStr.c_str();
        paramsUpdateTo[1] = toAccNumber.c_str();

        res = PQexecParams(
            db.conn,
            "UPDATE accounts SET balance = balance - $1::double precision "
            "WHERE number = $2",
            2,
            nullptr,
            paramsUpdateFrom,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            PQclear(res);
            PQexec(db.conn, "ROLLBACK");
            throw runtime_error("Ошибка списания со счета-отправителя.");
        }
        PQclear(res);

        res = PQexecParams(
            db.conn,
            "UPDATE accounts SET balance = balance + $1::double precision "
            "WHERE number = $2",
            2,
            nullptr,
            paramsUpdateTo,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            PQclear(res);
            PQexec(db.conn, "ROLLBACK");
            throw runtime_error("Ошибка зачисления на счёт-получатель.");
        }
        PQclear(res);

        // Получим новый баланс отправителя для ответа
        double newFromBalance = 0.0;
        if (!dbGetAccountBalance(db.conn, fromAccNumber, newFromBalance)) {
            PQexec(db.conn, "ROLLBACK");
            jsonError("Счёт-отправитель не найден после обновления (странно).");
            return;
        }

        res = PQexec(db.conn, "COMMIT");
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            PQclear(res);
            throw runtime_error("Ошибка коммита транзакции.");
        }
        PQclear(res);

        printJsonHeader();
        cout << "{ \"success\": true, "
             << "\"message\": \"Перевод выполнен.\", "
             << "\"newBalance\": " << newFromBalance << " }";

    } catch (const exception& e) {
        jsonError(string("Внутренняя ошибка (transfer): ") + e.what());
    }
}

// GET BALANCE
void handleGetBalance(Cgicc& cgi) {
    bool pAcc;
    string accNumber = getParam(cgi, "accountNumber", pAcc);
    if (!pAcc || accNumber.empty()) {
        jsonError("Не указан номер счёта.");
        return;
    }

    try {
        PgConn db;
        double balance = 0.0;
        if (!dbGetAccountBalance(db.conn, accNumber, balance)) {
            jsonError("Счёт не найден.");
            return;
        }

        printJsonHeader();
        cout << "{ \"success\": true, "
             << "\"balance\": " << balance << ", "
             << "\"message\": \"Баланс получен.\" }";

    } catch (const exception& e) {
        jsonError(string("Внутренняя ошибка (getBalance): ") + e.what());
    }
}

// ===================== MAIN =====================

int main() {
    try {
        srand(static_cast<unsigned>(time(nullptr)));

        Cgicc cgi;

        bool pAct;
        string action = getParam(cgi, "action", pAct);
        if (!pAct || action.empty()) {
            jsonError("Не указан параметр action.");
            return 0;
        }

        if      (action == "register")      handleRegister(cgi);
        else if (action == "login")         handleLogin(cgi);
        else if (action == "getAccounts")   handleGetAccounts(cgi);
        else if (action == "createAccount") handleCreateAccount(cgi);
        else if (action == "deleteAccount") handleDeleteAccount(cgi);
        else if (action == "topup")         handleTopup(cgi);
        else if (action == "withdraw")      handleWithdraw(cgi);
        else if (action == "transfer")      handleTransfer(cgi);
        else if (action == "getBalance")    handleGetBalance(cgi);
        else {
            jsonError("Неизвестное действие: " + action);
        }

    } catch (const exception& e) {
        jsonError(string("Внутренняя ошибка: ") + e.what());
    } catch (...) {
        jsonError("Неизвестная внутренняя ошибка.");
    }

    return 0;
}
