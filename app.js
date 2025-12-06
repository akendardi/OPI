// ================= НАСТРОЙКИ =================

// URL реального CGI-сервиса (когда будет готов back-end)
const API_URL = "/cgi-bin/bank.cgi";

// true  – тестовый режим, всё в localStorage
// false – обращаться к CGI по API_URL
const USE_MOCK = false;


// ================= "БД" ДЛЯ MOCK (localStorage) =================

function loadMockDb() {
    try {
        const raw = localStorage.getItem("mockBankDb");
        if (!raw) return { users: [], nextUserId: 1 };
        const db = JSON.parse(raw);
        // нормализуем accounts
        db.users.forEach(u => {
            if (!Array.isArray(u.accounts)) u.accounts = [];
        });
        return db;
    } catch {
        return { users: [], nextUserId: 1 };
    }
}

function saveMockDb() {
    localStorage.setItem("mockBankDb", JSON.stringify(mockDb));
}

let mockDb = loadMockDb();


// ================= ТЕКУЩИЙ ПОЛЬЗОВАТЕЛЬ =================

let currentUserId = null;
let currentUserName = null;
let currentAccounts = []; // массив счетов пользователя
let selectedAccountNumber = null;

function setCurrentUser(id, name, accounts) {
    currentUserId = id;
    currentUserName = name;
    currentAccounts = accounts || [];
    localStorage.setItem("currentUserId", String(id));
    localStorage.setItem("currentUserName", name || "");
}

function storeAccountsToLocal() {
    localStorage.setItem(
        "currentAccounts",
        JSON.stringify(currentAccounts || [])
    );
}

function loadCurrentUserFromStorage() {
    const idStr = localStorage.getItem("currentUserId");
    if (!idStr) return false;
    currentUserId = Number(idStr);
    currentUserName = localStorage.getItem("currentUserName") || ("Клиент " + idStr);
    try {
        const raw = localStorage.getItem("currentAccounts") || "[]";
        currentAccounts = JSON.parse(raw);
    } catch {
        currentAccounts = [];
    }
    return true;
}

function clearCurrentUser() {
    currentUserId = null;
    currentUserName = null;
    currentAccounts = [];
    selectedAccountNumber = null;
    localStorage.removeItem("currentUserId");
    localStorage.removeItem("currentUserName");
    localStorage.removeItem("currentAccounts");
}

function getSelectedAccount() {
    if (!selectedAccountNumber) return null;
    return currentAccounts.find(a => a.number === selectedAccountNumber) || null;
}


// ================= ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ UI =================

function $(id) {
    return document.getElementById(id);
}

function setStatus(id, text, type) {
    const el = $(id);
    if (!el) return;
    el.textContent = text || "";
    el.className = "status" + (type ? " " + type : "");
}

function updateUserInfoUI() {
    const block = $("userInfoBlock");
    if (!block) return;
    const nameEl = block.querySelector(".user-info__name");
    const idEl = block.querySelector(".user-info__id");
    if (!nameEl || !idEl) return;

    if (currentUserId) {
        nameEl.textContent = currentUserName || ("Клиент " + currentUserId);
        nameEl.classList.remove("text-muted");
        idEl.textContent = "ID клиента: " + currentUserId;
    } else {
        nameEl.textContent = "Гость";
        nameEl.classList.add("text-muted");
        idEl.textContent = "Не авторизован";
    }
}

function updateBalanceUI() {
    const balEl = $("balanceValue");
    const selLabel = $("selectedAccountLabel");
    const acc = getSelectedAccount();
    if (!balEl || !selLabel) return;

    if (!acc) {
        balEl.textContent = "—";
        selLabel.textContent = "не выбран";
    } else {
        balEl.textContent = Number(acc.balance || 0).toFixed(2);
        // красиво форматируем номер по 4 цифры
        selLabel.textContent = acc.number.replace(/(.{4})/g, "$1 ").trim();
    }
}

function renderAccounts() {
    const container = $("accountsList");
    if (!container) return;

    container.innerHTML = "";

    if (!currentAccounts || currentAccounts.length === 0) {
        container.innerHTML = `<p class="text-block text-muted">У вас пока нет ни одного счёта.</p>`;
        selectedAccountNumber = null;
        updateBalanceUI();
        return;
    }

    currentAccounts.forEach(acc => {
        const wrapper = document.createElement("div");
        wrapper.className = "account-item";
        const selected = acc.number === selectedAccountNumber;

        wrapper.innerHTML = `
            <div class="text-block">
                <strong>${acc.number.replace(/(.{4})/g, "$1 ").trim()}</strong><br>
                <span class="small text-muted">Баланс: ${Number(acc.balance || 0).toFixed(2)} ₽</span>
            </div>
            <div style="display:flex; gap:6px; flex-wrap:wrap; margin-top:4px;">
                <button class="btn btn-secondary btn-small" data-role="select" data-acc="${acc.number}">
                    ${selected ? "Выбран" : "Выбрать"}
                </button>
                <button class="btn btn-secondary btn-small" data-role="delete" data-acc="${acc.number}">
                    Удалить
                </button>
            </div>
        `;

        if (selected) {
            wrapper.style.border = "1px solid #22c55e";
        }

        container.appendChild(wrapper);
    });

    // обработчики
    container.querySelectorAll("button[data-role='select']").forEach(btn => {
        btn.addEventListener("click", () => {
            selectedAccountNumber = btn.dataset.acc;
            updateBalanceUI();
            renderAccounts(); // перерисовать, чтобы подсветить выбранный
        });
    });

    container.querySelectorAll("button[data-role='delete']").forEach(btn => {
        btn.addEventListener("click", () => {
            const accNumber = btn.dataset.acc;
            handleDeleteAccount(accNumber);
        });
    });
}


// ================= MOCK-СЕРВЕР =================

function findMockUserById(id) {
    const numId = Number(id);
    const u = mockDb.users.find(u => u.id === numId);
    if (u && !Array.isArray(u.accounts)) u.accounts = [];
    return u || null;
}

function findMockUserByLogin(login) {
    const asNum = Number(login);
    if (!Number.isNaN(asNum)) {
        return findMockUserById(asNum);
    }
    const u = mockDb.users.find(u => u.email === login);
    if (u && !Array.isArray(u.accounts)) u.accounts = [];
    return u || null;
}

function generateAccountNumber() {
    let base = "4000";
    let rest = "";
    for (let i = 0; i < 12; i++) {
        rest += Math.floor(Math.random() * 10);
    }
    return base + rest;
}

// --- REGISTER ---
function mockRegister({ fullName, email, password }) {
    if (!fullName || !email || !password || password.length < 6) {
        return { success: false, message: "Некорректные данные регистрации (MOCK)." };
    }
    if (mockDb.users.some(u => u.email === email)) {
        return { success: false, message: "Пользователь с таким email уже существует (MOCK)." };
    }
    const user = {
        id: mockDb.nextUserId++,
        fullName,
        email,
        password,
        accounts: [] // без счетов по умолчанию
    };
    mockDb.users.push(user);
    saveMockDb();
    return {
        success: true,
        message: "Регистрация выполнена (MOCK).",
        userId: user.id,
        fullName: user.fullName
    };
}

// --- LOGIN ---
function mockLogin({ login, password }) {
    const user = findMockUserByLogin(login);
    if (!user || user.password !== password) {
        return { success: false, message: "Неверный логин или пароль (MOCK)." };
    }
    return {
        success: true,
        message: "Вход выполнен (MOCK).",
        userId: user.id,
        fullName: user.fullName
    };
}

// --- GET ACCOUNTS ---
function mockGetAccounts({ userId }) {
    const user = findMockUserById(userId);
    if (!user) return { success: false, message: "Пользователь не найден (MOCK)." };
    return {
        success: true,
        accounts: user.accounts.map(a => ({ number: a.number, balance: a.balance }))
    };
}

// --- CREATE ACCOUNT ---
function mockCreateAccount({ userId }) {
    const user = findMockUserById(userId);
    if (!user) return { success: false, message: "Пользователь не найден (MOCK)." };
    if (user.accounts.length >= 3) {
        return { success: false, message: "Нельзя создать больше 3 счетов (MOCK)." };
    }
    const num = generateAccountNumber();
    user.accounts.push({ number: num, balance: 0 });
    saveMockDb();
    return {
        success: true,
        message: "Счёт создан (MOCK).",
        accountNumber: num
    };
}

// --- DELETE ACCOUNT ---
function mockDeleteAccount({ userId, accountNumber }) {
    const user = findMockUserById(userId);
    if (!user) return { success: false, message: "Пользователь не найден (MOCK)." };
    const acc = user.accounts.find(a => a.number === accountNumber);
    if (!acc) return { success: false, message: "Счёт не найден (MOCK)." };
    if (acc.balance > 0) {
        return { success: false, message: "Нельзя удалить счёт с ненулевым балансом (MOCK)." };
    }
    user.accounts = user.accounts.filter(a => a.number !== accountNumber);
    saveMockDb();
    return { success: true, message: "Счёт удалён (MOCK)." };
}

// --- TOPUP ---
function mockTopup({ accountNumber, amount }) {
    const sum = Number(amount);
    if (!(sum > 0)) return { success: false, message: "Сумма должна быть > 0 (MOCK)." };

    let acc = null;
    for (const u of mockDb.users) {
        acc = (u.accounts || []).find(a => a.number === accountNumber);
        if (acc) break;
    }
    if (!acc) return { success: false, message: "Счёт не найден (MOCK)." };
    acc.balance += sum;
    saveMockDb();
    return { success: true, message: "Баланс пополнен (MOCK).", newBalance: acc.balance };
}

// --- WITHDRAW ---
function mockWithdraw({ accountNumber, amount }) {
    const sum = Number(amount);
    if (!(sum > 0)) return { success: false, message: "Сумма должна быть > 0 (MOCK)." };

    let acc = null;
    for (const u of mockDb.users) {
        acc = (u.accounts || []).find(a => a.number === accountNumber);
        if (acc) break;
    }
    if (!acc) return { success: false, message: "Счёт не найден (MOCK)." };
    if (acc.balance < sum) return { success: false, message: "Недостаточно средств (MOCK)." };
    acc.balance -= sum;
    saveMockDb();
    return { success: true, message: "Снятие выполнено (MOCK).", newBalance: acc.balance };
}

// --- TRANSFER ---
function mockTransfer({ fromAccount, toAccount, amount }) {
    const sum = Number(amount);
    if (!(sum > 0)) return { success: false, message: "Сумма должна быть > 0 (MOCK)." };
    let fromAcc = null, toAcc = null;

    for (const u of mockDb.users) {
        (u.accounts || []).forEach(a => {
            if (a.number === fromAccount) fromAcc = a;
            if (a.number === toAccount) toAcc = a;
        });
    }

    if (!fromAcc) return { success: false, message: "Счёт-отправитель не найден (MOCK)." };
    if (!toAcc) return { success: false, message: "Счёт-получатель не найден (MOCK)." };
    if (fromAcc.balance < sum) return { success: false, message: "Недостаточно средств (MOCK)." };

    fromAcc.balance -= sum;
    toAcc.balance += sum;
    saveMockDb();
    return {
        success: true,
        message: "Перевод выполнен (MOCK).",
        newBalance: fromAcc.balance
    };
}

// --- GET BALANCE (для одного счёта) ---
function mockGetBalance({ accountNumber }) {
    let acc = null;
    for (const u of mockDb.users) {
        acc = (u.accounts || []).find(a => a.number === accountNumber);
        if (acc) break;
    }
    if (!acc) return { success: false, message: "Счёт не найден (MOCK)." };
    return { success: true, balance: acc.balance, message: "Баланс получен (MOCK)." };
}

// универсальный mock dispatcher
function mockPost(action, data) {
    switch (action) {
        case "register": return mockRegister(data);
        case "login": return mockLogin(data);
        case "getAccounts": return mockGetAccounts(data);
        case "createAccount": return mockCreateAccount(data);
        case "deleteAccount": return mockDeleteAccount(data);
        case "topup": return mockTopup(data);
        case "withdraw": return mockWithdraw(data);
        case "transfer": return mockTransfer(data);
        case "getBalance": return mockGetBalance(data);
        default:
            return { success: false, message: "Неизвестное действие (MOCK): " + action };
    }
}


// ================= ОТПРАВКА ЗАПРОСОВ (MOCK / CGI) =================

async function sendPostToServer(action, data) {
    if (USE_MOCK) {
        return new Promise(resolve => {
            setTimeout(() => resolve(mockPost(action, data)), 150);
        });
    }

    const body = new URLSearchParams({ action, ...data });
    const response = await fetch(API_URL, {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body: body.toString()
    });
    const text = await response.text();
    try {
        return JSON.parse(text);
    } catch {
        return { success: false, message: "Сервер вернул не JSON: " + text };
    }
}

async function fetchAccountsFromServer(userId) {
    if (USE_MOCK) {
        return sendPostToServer("getAccounts", { userId });
    }
    // CGI: GET /cgi-bin/bank.cgi?action=getAccounts&userId=...
    const resp = await fetch(`${API_URL}?action=getAccounts&userId=${encodeURIComponent(userId)}`);
    const text = await resp.text();
    try {
        return JSON.parse(text);
    } catch {
        return { success: false, message: "Сервер вернул не JSON: " + text };
    }
}

async function fetchBalanceFromServer(accountNumber) {
    if (USE_MOCK) {
        return sendPostToServer("getBalance", { accountNumber });
    }
    const resp = await fetch(`${API_URL}?action=getBalance&accountNumber=${encodeURIComponent(accountNumber)}`);
    const text = await resp.text();
    try {
        return JSON.parse(text);
    } catch {
        return { success: false, message: "Сервер вернул не JSON: " + text };
    }
}


// ================= ЛОГИКА AUTH.HTML =================

function initAuthPage() {
    const loginCard = $("loginCard");
    const registerCard = $("registerCard");
    const openRegBtn = $("toggleRegisterBtn");
    const openRegBtnWrapper = $("openRegBtnWrapper");
    const backBtn = $("backToLoginBtn");

    // открыть регистрацию
    if (openRegBtn && loginCard && registerCard && openRegBtnWrapper) {
        openRegBtn.addEventListener("click", () => {
            loginCard.classList.add("hidden");
            openRegBtnWrapper.classList.add("hidden");
            registerCard.classList.remove("hidden");
        });
    }

    // вернуться ко входу
    if (backBtn && loginCard && registerCard && openRegBtnWrapper) {
        backBtn.addEventListener("click", () => {
            registerCard.classList.add("hidden");
            loginCard.classList.remove("hidden");
            openRegBtnWrapper.classList.remove("hidden");
        });
    }

    // ВХОД
    const loginForm = $("loginForm");
    if (loginForm) {
        loginForm.addEventListener("submit", async (e) => {
            e.preventDefault();
            const login = $("loginLogin").value.trim();
            const password = $("loginPassword").value;

            if (!login || !password) {
                setStatus("loginStatus", "Введите логин и пароль.", "status--error");
                return;
            }

            setStatus("loginStatus", "Авторизация...", "status--info");
            const res = await sendPostToServer("login", { login, password });

            if (res.success) {
                setCurrentUser(res.userId, res.fullName || login, []);
                storeAccountsToLocal();
                window.location.href = "app.html";
            } else {
                setStatus("loginStatus", res.message || "Ошибка авторизации.", "status--error");
            }
        });
    }

    // РЕГИСТРАЦИЯ
    const registerForm = $("registerForm");
    if (registerForm) {
        registerForm.addEventListener("submit", async (e) => {
            e.preventDefault();
            const fullName = $("regFullName").value.trim();
            const email = $("regEmail").value.trim();
            const password = $("regPassword").value;

            if (!fullName || !email || password.length < 6) {
                setStatus("registerStatus", "Заполните все поля, пароль ≥ 6 символов.", "status--error");
                return;
            }

            setStatus("registerStatus", "Регистрация...", "status--info");
            const res = await sendPostToServer("register", { fullName, email, password });

            if (res.success) {
                setCurrentUser(res.userId, res.fullName || fullName, []);
                storeAccountsToLocal();
                window.location.href = "app.html";
            } else {
                setStatus("registerStatus", res.message || "Ошибка регистрации.", "status--error");
            }
        });
    }
}


// ================= ЛОГИКА APP.HTML (ЛИЧНЫЙ КАБИНЕТ) =================

async function initMainPage() {
    const hasUser = loadCurrentUserFromStorage();
    if (!hasUser) {
        window.location.href = "auth.html";
        return;
    }

    updateUserInfoUI();

    if ($("apiUrlText")) {
        $("apiUrlText").textContent = USE_MOCK ? "MOCK (localStorage)" : API_URL;
    }
    if ($("debugStatus")) {
        setStatus(
            "debugStatus",
            USE_MOCK ? "Работаю в режиме MOCK." : "Работаю с CGI: " + API_URL,
            "status--info"
        );
    }

    // выходим из аккаунта
    const logoutBtn = $("logoutBtn");
    if (logoutBtn) {
        logoutBtn.addEventListener("click", () => {
            clearCurrentUser();
            window.location.href = "auth.html";
        });
    }

    // получаем счета пользователя
    const res = await fetchAccountsFromServer(currentUserId);
    if (res.success) {
        currentAccounts = res.accounts || [];
        storeAccountsToLocal();
        if (currentAccounts.length > 0) {
            selectedAccountNumber = currentAccounts[0].number;
        }
    } else {
        setStatus("accountStatus", res.message || "Не удалось загрузить счета.", "status--error");
        currentAccounts = [];
        storeAccountsToLocal();
    }

    renderAccounts();
    updateBalanceUI();

    // СОЗДАТЬ СЧЕТ
    const createBtn = $("createAccountBtn");
    if (createBtn) {
        createBtn.addEventListener("click", async () => {
            if (currentAccounts.length >= 3) {
                setStatus("accountStatus", "Нельзя создать больше 3 счетов.", "status--error");
                return;
            }
            setStatus("accountStatus", "Создание счёта...", "status--info");
            const r = await sendPostToServer("createAccount", { userId: currentUserId });
            if (r.success) {
                // подгружаем счета заново
                const res2 = await fetchAccountsFromServer(currentUserId);
                if (res2.success) {
                    currentAccounts = res2.accounts || [];
                    storeAccountsToLocal();
                    selectedAccountNumber = r.accountNumber;
                    renderAccounts();
                    updateBalanceUI();
                    setStatus("accountStatus", r.message, "status--success");
                } else {
                    setStatus("accountStatus", res2.message || "Счёт создан, но не удалось обновить список.", "status--error");
                }
            } else {
                setStatus("accountStatus", r.message || "Ошибка при создании счёта.", "status--error");
            }
        });
    }

    // ПОПОЛНЕНИЕ
    const topupForm = $("topupForm");
    if (topupForm) {
        topupForm.addEventListener("submit", async (e) => {
            e.preventDefault();
            if (!selectedAccountNumber) {
                setStatus("topupStatus", "Сначала выберите счёт.", "status--error");
                return;
            }
            const amount = parseFloat($("topupAmount").value);
            if (!(amount > 0)) {
                setStatus("topupStatus", "Введите положительную сумму.", "status--error");
                return;
            }
            setStatus("topupStatus", "Пополнение...", "status--info");
            const r = await sendPostToServer("topup", {
                accountNumber: selectedAccountNumber,
                amount
            });
            if (r.success) {
                // обновляем баланс конкретного счёта
                const acc = getSelectedAccount();
                if (acc) acc.balance = r.newBalance ?? (Number(acc.balance) + amount);
                storeAccountsToLocal();
                renderAccounts();
                updateBalanceUI();
                setStatus("topupStatus", r.message || "Баланс пополнен.", "status--success");
            } else {
                setStatus("topupStatus", r.message || "Ошибка при пополнении.", "status--error");
            }
        });
    }

    // СНЯТИЕ
    const withdrawForm = $("withdrawForm");
    if (withdrawForm) {
        withdrawForm.addEventListener("submit", async (e) => {
            e.preventDefault();
            if (!selectedAccountNumber) {
                setStatus("withdrawStatus", "Сначала выберите счёт.", "status--error");
                return;
            }
            const amount = parseFloat($("withdrawAmount").value);
            if (!(amount > 0)) {
                setStatus("withdrawStatus", "Введите положительную сумму.", "status--error");
                return;
            }
            setStatus("withdrawStatus", "Снятие...", "status--info");
            const r = await sendPostToServer("withdraw", {
                accountNumber: selectedAccountNumber,
                amount
            });
            if (r.success) {
                const acc = getSelectedAccount();
                if (acc) acc.balance = r.newBalance ?? (Number(acc.balance) - amount);
                storeAccountsToLocal();
                renderAccounts();
                updateBalanceUI();
                setStatus("withdrawStatus", r.message || "Снятие выполнено.", "status--success");
            } else {
                setStatus("withdrawStatus", r.message || "Ошибка при снятии.", "status--error");
            }
        });
    }

    // ПЕРЕВОД
    const transferForm = $("transferForm");
    if (transferForm) {
        transferForm.addEventListener("submit", async (e) => {
            e.preventDefault();
            if (!selectedAccountNumber) {
                setStatus("transferStatus", "Сначала выберите счёт-отправитель.", "status--error");
                return;
            }
            const to = $("transferTo").value.trim();
            const amount = parseFloat($("transferAmount").value);
            if (!/^\d{16}$/.test(to)) {
                setStatus("transferStatus", "Номер счёта получателя должен содержать 16 цифр.", "status--error");
                return;
            }
            if (!(amount > 0)) {
                setStatus("transferStatus", "Введите положительную сумму.", "status--error");
                return;
            }
            setStatus("transferStatus", "Перевод...", "status--info");
            const r = await sendPostToServer("transfer", {
                fromAccount: selectedAccountNumber,
                toAccount: to,
                amount
            });
            if (r.success) {
                const acc = getSelectedAccount();
                if (acc) acc.balance = r.newBalance ?? (Number(acc.balance) - amount);
                storeAccountsToLocal();
                renderAccounts();
                updateBalanceUI();
                setStatus("transferStatus", r.message || "Перевод выполнен.", "status--success");
            } else {
                setStatus("transferStatus", r.message || "Ошибка при переводе.", "status--error");
            }
        });
    }

    // ОБНОВИТЬ БАЛАНС (GET)
    const refreshBtn = $("refreshBalanceBtn");
    if (refreshBtn) {
        refreshBtn.addEventListener("click", async () => {
            const acc = getSelectedAccount();
            if (!acc) {
                setStatus("transferStatus", "Сначала выберите счёт.", "status--error");
                return;
            }
            setStatus("transferStatus", "Запрос баланса...", "status--info");
            const r = await fetchBalanceFromServer(acc.number);
            if (r.success) {
                acc.balance = r.balance ?? acc.balance;
                storeAccountsToLocal();
                renderAccounts();
                updateBalanceUI();
                setStatus("transferStatus", r.message || "Баланс обновлён.", "status--success");
            } else {
                setStatus("transferStatus", r.message || "Ошибка при запросе баланса.", "status--error");
            }
        });
    }
}

// удаление счета (с проверкой баланса и подтверждением)
async function handleDeleteAccount(accountNumber) {
    const acc = currentAccounts.find(a => a.number === accountNumber);
    if (!acc) {
        setStatus("accountStatus", "Счёт не найден.", "status--error");
        return;
    }
    if (acc.balance > 0) {
        setStatus("accountStatus", "Нельзя удалить счёт с ненулевым балансом.", "status--error");
        return;
    }

    const formatted = accountNumber.replace(/(.{4})/g, "$1 ").trim();
    const ok = window.confirm(
        `Вы уверены, что хотите удалить счёт ${formatted}?\nЭто действие нельзя отменить.`
    );
    if (!ok) return;

    setStatus("accountStatus", "Удаление счёта...", "status--info");
    const r = await sendPostToServer("deleteAccount", {
        userId: currentUserId,
        accountNumber
    });

    if (r.success) {
        // обновляем список
        const res = await fetchAccountsFromServer(currentUserId);
        if (res.success) {
            currentAccounts = res.accounts || [];
            storeAccountsToLocal();
            if (selectedAccountNumber === accountNumber) {
                selectedAccountNumber = currentAccounts[0]?.number || null;
            }
            renderAccounts();
            updateBalanceUI();
            setStatus("accountStatus", r.message, "status--success");
        } else {
            setStatus("accountStatus", res.message || "Счёт удалён, но список не обновлён.", "status--error");
        }
    } else {
        setStatus("accountStatus", r.message || "Ошибка при удалении счёта.", "status--error");
    }
}


// ================= ТОЧКА ВХОДА =================

document.addEventListener("DOMContentLoaded", () => {
    const isAuth = document.getElementById("authPage");
    const isMain = document.getElementById("mainPage");

    if (isAuth) {
        initAuthPage();
    }
    if (isMain) {
        initMainPage();
    }
});
