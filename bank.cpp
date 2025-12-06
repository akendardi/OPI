#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <algorithm>

#include <cgicc/Cgicc.h>
#include <cgicc/HTTPPlainHeader.h>

using namespace std;
using namespace cgicc;

// ===================== "БД" НА ФАЙЛЕ =====================

// Файл, где будем хранить пользователей и счета (временно вместо PostgreSQL)
const char* DB_PATH = "/tmp/bankdb.txt";

struct Account {
    string number;   // 16-значный номер
    double balance;  // баланс
};

struct User {
    int id;
    string fullName;
    string email;
    string password;
    vector<Account> accounts;
};

static vector<User> g_users;
static int g_nextUserId = 1;

// простенький split по символу
vector<string> split(const string& s, char delim) {
    vector<string> parts;
    string item;
    stringstream ss(s);
    while (getline(ss, item, delim)) {
        parts.push_back(item);
    }
    return parts;
}

// загрузка "БД" из файла
void loadDb() {
    g_users.clear();
    g_nextUserId = 1;

    ifstream f(DB_PATH);
    if (!f.is_open()) {
        return; // файла нет — первая загрузка
    }

    string line;
    int maxId = 0;

    while (getline(f, line)) {
        if (line.empty()) continue;
        auto parts = split(line, '|');
        if (parts.size() == 0) continue;

        if (parts[0] == "USER" && parts.size() >= 5) {
            User u;
            u.id       = stoi(parts[1]);
            u.fullName = parts[2];
            u.email    = parts[3];
            u.password = parts[4];
            g_users.push_back(u);
            if (u.id > maxId) maxId = u.id;
        } else if (parts[0] == "ACC" && parts.size() >= 4) {
            int userId = stoi(parts[1]);
            string number = parts[2];
            double balance = stod(parts[3]);
            auto it = find_if(g_users.begin(), g_users.end(),
                              [userId](const User& u){ return u.id == userId; });
            if (it != g_users.end()) {
                it->accounts.push_back(Account{number, balance});
            }
        }
    }

    if (maxId > 0) {
        g_nextUserId = maxId + 1;
    }
}

// сохранение "БД" в файл
void saveDb() {
    ofstream f(DB_PATH, ios::trunc);
    if (!f.is_open()) {
        // На реальном проекте тут логируем ошибку
        return;
    }
    for (const auto& u : g_users) {
        f << "USER|" << u.id << "|" << u.fullName << "|" << u.email << "|" << u.password << "\n";
        for (const auto& acc : u.accounts) {
            f << "ACC|" << u.id << "|" << acc.number << "|" << acc.balance << "\n";
        }
    }
}

// генерация 16-значного номера счёта (как в mock: 4000 + 12 цифр)
string generateAccountNumber() {
    string num = "4000";
    for (int i = 0; i < 12; ++i) {
        int d = rand() % 10;
        num.push_back('0' + d);
    }
    return num;
}

User* findUserById(int id) {
    for (auto& u : g_users) {
        if (u.id == id) return &u;
    }
    return nullptr;
}

// login: может быть либо ID, либо email
User* findUserByLogin(const string& login) {
    // если login — чисто число, пробуем как id
    bool allDigits = !login.empty();
    for (char c : login) {
        if (!isdigit((unsigned char)c)) { allDigits = false; break; }
    }
    if (allDigits) {
        int id = stoi(login);
        return findUserById(id);
    }
    // иначе ищем по email
    for (auto& u : g_users) {
        if (u.email == login) return &u;
    }
    return nullptr;
}

// поиск счёта по номеру
Account* findAccountByNumber(const string& number, User** ownerOut = nullptr) {
    for (auto& u : g_users) {
        for (auto& acc : u.accounts) {
            if (acc.number == number) {
                if (ownerOut) *ownerOut = &u;
                return &acc;
            }
        }
    }
    return nullptr;
}

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

// ===================== HANDLERS (как в mock в JS) =====================

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

    // проверка на уникальность email
    for (const auto& u : g_users) {
        if (u.email == email) {
            jsonError("Пользователь с таким email уже существует.");
            return;
        }
    }

    User u;
    u.id = g_nextUserId++;
    u.fullName = fullName;
    u.email = email;
    u.password = password;
    g_users.push_back(u);
    saveDb();

    printJsonHeader();
    cout << "{ \"success\": true, "
         << "\"message\": \"Регистрация выполнена.\", "
         << "\"userId\": " << u.id << ", "
         << "\"fullName\": \"" << u.fullName << "\" }";
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

    User* u = findUserByLogin(login);
    if (!u || u->password != password) {
        jsonError("Неверный логин или пароль.");
        return;
    }

    printJsonHeader();
    cout << "{ \"success\": true, "
         << "\"message\": \"Вход выполнен.\", "
         << "\"userId\": " << u->id << ", "
         << "\"fullName\": \"" << u->fullName << "\" }";
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

    User* u = findUserById(userId);
    if (!u) {
        jsonError("Пользователь не найден.");
        return;
    }

    printJsonHeader();
    cout << "{ \"success\": true, \"accounts\": [";

    for (size_t i = 0; i < u->accounts.size(); ++i) {
        const auto& acc = u->accounts[i];
        if (i > 0) cout << ", ";
        cout << "{ \"number\": \"" << acc.number << "\", "
             << "\"balance\": " << acc.balance << " }";
    }

    cout << "] }";
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

    User* u = findUserById(userId);
    if (!u) {
        jsonError("Пользователь не найден.");
        return;
    }

    if (u->accounts.size() >= 3) {
        jsonError("Нельзя создать больше 3 счетов.");
        return;
    }

    string num = generateAccountNumber();
    u->accounts.push_back(Account{num, 0.0});
    saveDb();

    printJsonHeader();
    cout << "{ \"success\": true, "
         << "\"message\": \"Счёт создан.\", "
         << "\"accountNumber\": \"" << num << "\" }";
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

    User* u = findUserById(userId);
    if (!u) {
        jsonError("Пользователь не найден.");
        return;
    }

    auto it = find_if(u->accounts.begin(), u->accounts.end(),
                      [&accNumber](const Account& a){ return a.number == accNumber; });
    if (it == u->accounts.end()) {
        jsonError("Счёт не найден.");
        return;
    }

    if (it->balance > 0.0) {
        jsonError("Нельзя удалить счёт с ненулевым балансом.");
        return;
    }

    u->accounts.erase(it);
    saveDb();

    jsonOkMessage("Счёт удалён.");
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

    User* owner = nullptr;
    Account* acc = findAccountByNumber(accNumber, &owner);
    if (!acc) {
        jsonError("Счёт не найден.");
        return;
    }

    acc->balance += amount;
    saveDb();

    printJsonHeader();
    cout << "{ \"success\": true, "
         << "\"message\": \"Баланс пополнен.\", "
         << "\"newBalance\": " << acc->balance << " }";
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

    User* owner = nullptr;
    Account* acc = findAccountByNumber(accNumber, &owner);
    if (!acc) {
        jsonError("Счёт не найден.");
        return;
    }

    if (acc->balance < amount) {
        jsonError("Недостаточно средств.");
        return;
    }

    acc->balance -= amount;
    saveDb();

    printJsonHeader();
    cout << "{ \"success\": true, "
         << "\"message\": \"Снятие выполнено.\", "
         << "\"newBalance\": " << acc->balance << " }";
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

    User* ownerFrom = nullptr;
    User* ownerTo   = nullptr;
    Account* accFrom = findAccountByNumber(fromAccNumber, &ownerFrom);
    Account* accTo   = findAccountByNumber(toAccNumber,   &ownerTo);

    if (!accFrom) {
        jsonError("Счёт-отправитель не найден.");
        return;
    }
    if (!accTo) {
        jsonError("Счёт-получатель не найден.");
        return;
    }

    if (accFrom->balance < amount) {
        jsonError("Недостаточно средств.");
        return;
    }

    accFrom->balance -= amount;
    accTo->balance   += amount;
    saveDb();

    printJsonHeader();
    cout << "{ \"success\": true, "
         << "\"message\": \"Перевод выполнен.\", "
         << "\"newBalance\": " << accFrom->balance << " }";
}

// GET BALANCE (по номеру счёта)
void handleGetBalance(Cgicc& cgi) {
    bool pAcc;
    string accNumber = getParam(cgi, "accountNumber", pAcc);
    if (!pAcc || accNumber.empty()) {
        jsonError("Не указан номер счёта.");
        return;
    }

    User* owner = nullptr;
    Account* acc = findAccountByNumber(accNumber, &owner);
    if (!acc) {
        jsonError("Счёт не найден.");
        return;
    }

    printJsonHeader();
    cout << "{ \"success\": true, "
         << "\"balance\": " << acc->balance << ", "
         << "\"message\": \"Баланс получен.\" }";
}

// ===================== MAIN =====================

int main() {
    try {
        // инициализация rand для генерации номеров счетов
        srand(static_cast<unsigned>(time(nullptr)));

        // загружаем "БД" из файла при каждом запросе
        loadDb();

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

    } catch (exception& e) {
        jsonError(string("Внутренняя ошибка: ") + e.what());
    } catch (...) {
        jsonError("Неизвестная внутренняя ошибка.");
    }

    return 0;
}
