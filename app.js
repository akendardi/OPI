// ===== НАСТРОЙКИ =====

// URL реального CGI-сервиса (когда будет готов back-end)
const API_URL = "/cgi-bin/bank.cgi";

// true  – тестовый режим, всё хранится в localStorage (можно открывать файлы напрямую)
// false – работать через CGI (Apache + C++ + PostgreSQL)
const USE_MOCK = true;

// ===== "БД" ДЛЯ MOCK (localStorage) =====

function loadMockDb() {
    try {
        const raw = localStorage.getItem("mockBankDb");
        if (!raw) return { users: [], nextUserId: 1 };
        return JSON.parse(raw);
    } catch {
        return { users: [], nextUserId: 1 };
    }
}

function saveMockDb() {
    localStorage.setItem("mockBankDb", JSON.stringify(mockDb));
}

let mockDb = loadMockDb();

// ===== ТЕКУЩИЙ ПОЛЬЗОВАТЕЛЬ (глобальные переменные + localStorage) =====

let currentUserId = null;
let currentUserName = null;
let currentBalance = null;

function setCurrentUser(id, name, balance) {
    currentUserId = id;
    currentUserName = name;
    currentBalance = balance;
    localStorage.setItem("currentUserId", String(id));
    localStorage.setItem("currentUserName", name || "");
    if (balance != null) {
        localStorage.setItem("currentBalance", String(balance));
    }
}

function loadCurrentUserFromStorage() {
    const idStr = localStorage.getItem("currentUserId");
    if (!idStr) return false;
    currentUserId = Number(idStr);
    currentUserName = localStorage.getItem("currentUserName") || ("Клиент " + idStr);
    const balStr = localStorage.getItem("currentBalance");
    currentBalance = balStr != null ? Number(balStr) : null;
    return true;
}

function clearCurrentUser() {
    currentUserId = null;
    currentUserName = null;
    currentBalance = null;
    localStorage.removeItem("currentUserId");
    localStorage.removeItem("currentUserName");
    localStorage.removeItem("currentBalance");
}

// ===== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ UI =====

function $(id) { return document.getElementById(id); }

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
    const el = $("balanceValue");
    if (!el) return;
    if (currentBalance == null) {
        el.textContent = "—";
    } else {
        el.textContent = Number(currentBalance).toFixed(2);
    }
}

// ===== MOCK-СЕРВЕР =====

function findUserById(id) {
    const numId = Number(id);
    return mockDb.users.find(u => u.id === numId) || null;
}
function findUserByLogin(login) {
    const asNum = Number(login);
    if (!Number.isNaN(asNum)) {
        return findUserById(asNum);
    }
    return mockDb.users.find(u => u.email === login) || null;
}

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
        password, // в реальном сервере — хэш, а не пароль
        balance: 0
    };
    mockDb.users.push(user);
    saveMockDb();
    return {
        success: true,
        message: "Регистрация выполнена (MOCK).",
        userId: user.id,
        fullName: user.fullName,
        balance: user.balance
    };
}

function mockLogin({ login, password }) {
    const user = findUserByLogin(login);
    if (!user || user.password !== password) {
        return { success: false, message: "Неверный логин или пароль (MOCK)." };
    }
    return {
        success: true,
        message: "Вход выполнен (MOCK).",
        userId: user.id,
        fullName: user.fullName,
        balance: user.balance
    };
}

function mockTopup({ userId, amount }) {
    const user = findUserById(userId);
    const sum = Number(amount);
    if (!user) return { success: false, message: "Пользователь не найден (MOCK)." };
    if (!(sum > 0)) return { success: false, message: "Сумма должна быть положительной (MOCK)." };
    user.balance += sum;
    saveMockDb();
    return { success: true, message: "Баланс пополнен (MOCK).", balance: user.balance };
}

function mockWithdraw({ userId, amount }) {
    const user = findUserById(userId);
    const sum = Number(amount);
    if (!user) return { success: false, message: "Пользователь не найден (MOCK)." };
    if (!(sum > 0)) return { success: false, message: "Сумма должна быть положительной (MOCK)." };
    if (user.balance < sum) return { success: false, message: "Недостаточно средств (MOCK)." };
    user.balance -= sum;
    saveMockDb();
    return { success: true, message: "Снятие выполнено (MOCK).", balance: user.balance };
}

function mockTransfer({ fromUserId, toUserId, amount }) {
    const fromUser = findUserById(fromUserId);
    const toUser = findUserById(toUserId);
    const sum = Number(amount);
    if (!fromUser) return { success: false, message: "Отправитель не найден (MOCK)." };
    if (!toUser) return { success: false, message: "Получатель не найден (MOCK)." };
    if (!(sum > 0)) return { success: false, message: "Сумма должна быть положительной (MOCK)." };
    if (fromUser.balance < sum) return { success: false, message: "Недостаточно средств (MOCK)." };
    fromUser.balance -= sum;
    toUser.balance += sum;
    saveMockDb();
    return {
        success: true,
        message: `Перевод ${sum.toFixed(2)} ₽ пользователю ID=${toUser.id} (MOCK).`,
        balance: fromUser.balance
    };
}

function mockGetBalance(userId) {
    const user = findUserById(userId);
    if (!user) return { success: false, message: "Пользователь не найден (MOCK)." };
    return { success: true, message: "Баланс получен (MOCK).", balance: user.balance };
}

// ===== ОБЩИЕ ФУНКЦИИ ЗАПРОСОВ (MOCK / CGI) =====

async function sendPostToServer(action, data) {
    if (USE_MOCK) {
        return new Promise(resolve => {
            setTimeout(() => {
                let res;
                switch (action) {
                    case "register": res = mockRegister(data); break;
                    case "login": res = mockLogin(data); break;
                    case "topup": res = mockTopup(data); break;
                    case "withdraw": res = mockWithdraw(data); break;
                    case "transfer": res = mockTransfer(data); break;
                    default: res = { success: false, message: "Неизвестное действие (MOCK)." };
                }
                resolve(res);
            }, 150);
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

async function fetchBalanceByGet(userId) {
    if (USE_MOCK) {
        return new Promise(resolve => {
            setTimeout(() => resolve(mockGetBalance(userId)), 150);
        });
    }

    const response = await fetch(`${API_URL}?action=getBalance&userId=${encodeURIComponent(userId)}`);
    const text = await response.text();
    try {
        return JSON.parse(text);
    } catch {
        return { success: false, message: "Сервер вернул не JSON: " + text };
    }
}

// ===== ИНИЦИАЛИЗАЦИЯ СТРАНИЦ =====

// Страница auth.html
// Страница auth.html
function initAuthPage() {
    const loginCard = $("loginCard");
    const registerCard = $("registerCard");
    const openRegBtn = $("toggleRegisterBtn");
    const openRegBtnWrapper = $("openRegBtnWrapper");
    const backBtn = $("backToLoginBtn");

    // Открыть регистрацию
    openRegBtn.addEventListener("click", () => {
        loginCard.classList.add("hidden");          // скрыть вход
        openRegBtnWrapper.classList.add("hidden");  // скрыть кнопку сверху
        registerCard.classList.remove("hidden");    // показать регистрацию
    });

    // Вернуться ко входу
    backBtn.addEventListener("click", () => {
        registerCard.classList.add("hidden");       // скрыть регистрацию
        loginCard.classList.remove("hidden");       // показать вход
        openRegBtnWrapper.classList.remove("hidden"); // вернуть кнопку "Создать аккаунт"
    });

    // Авторизация
    $("loginForm").addEventListener("submit", async (e) => {
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
            setCurrentUser(res.userId, res.fullName || login, res.balance ?? 0);
            window.location.href = "app.html";
        } else {
            setStatus("loginStatus", res.message, "status--error");
        }
    });


    $("registerForm").addEventListener("submit", async (e) => {
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
            setCurrentUser(res.userId, fullName, res.balance ?? 0);
            window.location.href = "app.html";
        } else {
            setStatus("registerStatus", res.message, "status--error");
        }
    });
}





// Страница app.html
function initMainPage() {
    const ok = loadCurrentUserFromStorage();
    if (!ok) {
        // если не авторизован – отправляем на страницу входа
        window.location.href = "auth.html";
        return;
    }

    updateUserInfoUI();
    updateBalanceUI();

    if ($("apiUrlText")) {
        $("apiUrlText").textContent = USE_MOCK ? "MOCK (localStorage)" : API_URL;
    }
    if ($("debugStatus")) {
        setStatus(
            "debugStatus",
            USE_MOCK
                ? "Работаю в режиме MOCK, сервер не требуется."
                : "Ожидаю CGI по адресу: " + API_URL,
            "status--info"
        );
    }

    const logoutBtn = $("logoutBtn");
    if (logoutBtn) {
        logoutBtn.addEventListener("click", () => {
            clearCurrentUser();
            window.location.href = "auth.html";
        });
    }

    // Пополнение
    const topupForm = $("topupForm");
    if (topupForm) {
        topupForm.addEventListener("submit", async (e) => {
            e.preventDefault();
            const amount = parseFloat($("topupAmount").value);
            if (!(amount > 0)) {
                setStatus("topupStatus", "Введите положительную сумму.", "status--error");
                return;
            }
            setStatus("topupStatus", "Отправка запроса...", "status--info");
            const res = await sendPostToServer("topup", { userId: currentUserId, amount });
            if (res.success) {
                currentBalance = res.balance ?? (currentBalance + amount);
                localStorage.setItem("currentBalance", String(currentBalance));
                updateBalanceUI();
                setStatus("topupStatus", res.message || "Баланс пополнен.", "status--success");
            } else {
                setStatus("topupStatus", res.message || "Ошибка при пополнении.", "status--error");
            }
        });
    }

    // Снятие
    const withdrawForm = $("withdrawForm");
    if (withdrawForm) {
        withdrawForm.addEventListener("submit", async (e) => {
            e.preventDefault();
            const amount = parseFloat($("withdrawAmount").value);
            if (!(amount > 0)) {
                setStatus("withdrawStatus", "Введите положительную сумму.", "status--error");
                return;
            }
            setStatus("withdrawStatus", "Отправка запроса...", "status--info");
            const res = await sendPostToServer("withdraw", { userId: currentUserId, amount });
            if (res.success) {
                currentBalance = res.balance ?? (currentBalance - amount);
                localStorage.setItem("currentBalance", String(currentBalance));
                updateBalanceUI();
                setStatus("withdrawStatus", res.message || "Снятие выполнено.", "status--success");
            } else {
                setStatus("withdrawStatus", res.message || "Ошибка при снятии.", "status--error");
            }
        });
    }

    // Перевод
    const transferForm = $("transferForm");
    if (transferForm) {
        transferForm.addEventListener("submit", async (e) => {
            e.preventDefault();
            const toUserId = $("transferTo").value.trim();
            const amount = parseFloat($("transferAmount").value);
            if (!toUserId || !(amount > 0)) {
                setStatus("transferStatus", "Введите ID получателя и сумму.", "status--error");
                return;
            }
            setStatus("transferStatus", "Отправка перевода...", "status--info");
            const res = await sendPostToServer("transfer", {
                fromUserId: currentUserId,
                toUserId,
                amount
            });
            if (res.success) {
                currentBalance = res.balance ?? (currentBalance - amount);
                localStorage.setItem("currentBalance", String(currentBalance));
                updateBalanceUI();
                setStatus("transferStatus", res.message || "Перевод выполнен.", "status--success");
            } else {
                setStatus("transferStatus", res.message || "Ошибка при переводе.", "status--error");
            }
        });
    }

    // Обновить баланс (GET)
    const refreshBtn = $("refreshBalanceBtn");
    if (refreshBtn) {
        refreshBtn.addEventListener("click", async () => {
            setStatus("transferStatus", "Запрос баланса (GET)...", "status--info");
            const res = await fetchBalanceByGet(currentUserId);
            if (res.success) {
                currentBalance = res.balance ?? currentBalance;
                localStorage.setItem("currentBalance", String(currentBalance));
                updateBalanceUI();
                setStatus("transferStatus", res.message || "Баланс обновлён.", "status--success");
            } else {
                setStatus("transferStatus", res.message || "Ошибка при запросе баланса.", "status--error");
            }
        });
    }
}

// ===== ТОЧКА ВХОДА =====

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
