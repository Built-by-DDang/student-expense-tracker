#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <limits>
#include <vector>
#include <iomanip>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cctype>

using namespace std;

const char* const EXPENSE_FILE = "studentExpenses.txt";
const char* const TEMP_FILE = "studentExpenses.txt.tmp";
const char* const BACKUP_FILE = "studentExpenses.txt.bak";

struct Expense
{
    int id = 0;
    string date;
    string category;
    string description;
    long long amount = 0; // in integer cents, e.g. 100 = $1.00
};

struct DeletedRecord
{
    Expense expense;
    size_t originalIndex = 0;
};

vector<DeletedRecord> g_undoStack;

string formatCents(long long cents);
bool parseAmount(const string& input, long long& amount);
bool parseStoredAmount(const string& input, long long& amount);
bool parseDate(const string& date, int& year, int& month, int& day);
bool readExpenseDate(string& date);
bool readRepeatedExpense(Expense& expense);
bool splitLine(const string& report, Expense& expense);
string expenseToLine(const Expense& expense);
bool rewriteExpenses(const vector<string>& lines);
int getNextExpenseId();
bool performUndo(bool requireConfirmation);
void deleteExpense();

const char* const BUDGET_FILE = "studentBudgets.txt";
const char* const BUDGET_TEMP_FILE = "studentBudgets.txt.tmp";
const char* const BUDGET_BACKUP_FILE = "studentBudgets.txt.bak";

struct Budget
{
    string type; // "TIMEFRAME" or "CATEGORY"
    string name; // "WEEK", "MONTH", "YEAR", or normalized category name
    long long amount = 0; // in integer cents, e.g. 10000 = $100.00
};

// Clear only when moving to another screen, never while an error is being read.
void clearScreen()
{
#ifdef _WIN32
    std::system("cls");
#else
    cout << "\x1b[2J\x1b[H" << flush;
#endif
}

// Keep results visible until the user is ready for the main menu.
bool waitForEnter()
{
    cout << "\nPress Enter to return to the main menu..." << flush;
    string input;
    return static_cast<bool>(getline(cin, input));
}

// Read a whole line so input such as "2abc" is not accepted as 2.
// Returning false lets callers stop cleanly if the input stream ends.
bool readInteger(const string& prompt, int minimum, int maximum, int& value)
{
    string input;
    while (cout << prompt, getline(cin, input))
    {
        size_t start = input.find_first_not_of(" \t\r\n");
        if (start == string::npos)
            continue;
        istringstream stream(input.substr(start));
        int candidate;
        if (stream >> candidate)
        {
            stream >> ws;
            if (stream.eof() && candidate >= minimum && candidate <= maximum)
            {
                value = candidate;
                return true;
            }
        }
        cout << "Enter a whole number from " << minimum << " to " << maximum << ".\n";
    }
    return false;
}

int daysInMonth(int year, int month)
{
    const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 0;
    const bool leapYear = (year % 400 == 0) || (year % 4 == 0 && year % 100 != 0);
    return (month == 2 && leapYear) ? 29 : days[month - 1];
}

bool readDate(string& date)
{
    int year = 0, month = 0, day = 0;
    if (!readInteger("Enter the year (enter 0 to cancel): ", 0, 9999, year) || year == 0)
        return false;
    if (!readInteger("Enter the month (enter 0 to cancel): ", 0, 12, month) || month == 0)
        return false;
    if (!readInteger("Enter the day (enter 0 to cancel): ", 0, daysInMonth(year, month), day) || day == 0)
        return false;

    ostringstream formatted;
    formatted << setfill('0') << setw(4) << year << '-'
              << setw(2) << month << '-' << setw(2) << day;
    date = formatted.str();
    return true;
}

bool readText(const string& prompt, string& value)
{
    string input;
    while (cout << prompt, getline(cin, input))
    {
        size_t start = input.find_first_not_of(" \t\r\n");
        if (start == string::npos)
            continue;
        if (input.find('|') == string::npos)
        {
            value = input.substr(start, input.find_last_not_of(" \t\r\n") - start + 1);
            return true;
        }
        cout << "Enter nonempty text without the | character.\n";
    }
    return false;
}

// Normalize a category string to Title Case so "food", "FOOD", and "Food"
// all become "Food".  Trims whitespace and collapses multiple spaces.
string normalizeCategory(const string& raw)
{
    size_t start = raw.find_first_not_of(" \t\r\n");
    if (start == string::npos)
        return "";
    size_t end = raw.find_last_not_of(" \t\r\n");

    string result;
    result.reserve(end - start + 1);
    bool capitalizeNext = true;
    bool inSpace = false;

    for (size_t i = start; i <= end; ++i)
    {
        unsigned char ch = static_cast<unsigned char>(raw[i]);
        if (isspace(ch))
        {
            if (!inSpace)
            {
                result.push_back(' ');
                inSpace = true;
                capitalizeNext = true;
            }
        }
        else
        {
            inSpace = false;
            if (capitalizeNext)
            {
                result.push_back(static_cast<char>(toupper(ch)));
                capitalizeNext = false;
            }
            else
            {
                result.push_back(static_cast<char>(tolower(ch)));
            }
        }
    }
    return result;
}

// Convert a string to all lowercase for case-insensitive comparisons.
string toLowerStr(const string& s)
{
    string result = s;
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<char>(tolower(static_cast<unsigned char>(result[i])));
    return result;
}

// Asks a yes-or-no question.  Returns true for yes, false for no or EOF.
bool confirmAction(const string& prompt)
{
    string input;
    while (cout << prompt, getline(cin, input))
    {
        size_t pos = input.find_first_not_of(" \t\r\n");
        if (pos == string::npos)
            continue;
        char first = static_cast<char>(tolower(static_cast<unsigned char>(input[pos])));
        if (first == 'y')
            return true;
        if (first == 'n')
            return false;
        cout << "Please enter y or n.\n";
    }
    return false;
}

// Collect unique, normalized category names from the expense file.
bool loadExistingCategories(vector<string>& categories)
{
    categories.clear();
    ifstream file(EXPENSE_FILE);
    if (!file)
        return false;

    string line;
    while (getline(file, line))
    {
        Expense expense;
        if (!splitLine(line, expense))
            continue;
        string cat = normalizeCategory(expense.category);
        if (cat.find_first_not_of(" \t\r\n") == string::npos)
            continue;
        bool found = false;
        for (const string& existing : categories)
        {
            if (existing == cat)
            {
                found = true;
                break;
            }
        }
        if (!found)
            categories.push_back(cat);
    }
    return true;
}

// Let the user pick from previously used categories or create a new one.
bool readCategory(string& category)
{
    vector<string> existing;
    loadExistingCategories(existing);

    if (existing.empty())
    {
        if (!readText("Enter category (enter 0 to cancel): ", category) || category == "0")
            return false;
        category = normalizeCategory(category);
        return !category.empty();
    }

    cout << "Choose an existing category or create a new one:\n\n";
    for (size_t i = 0; i < existing.size(); ++i)
        cout << "  " << (i + 1) << ". " << existing[i] << "\n";
    cout << "  " << (existing.size() + 1) << ". Create a new category\n"
         << "  0. Cancel\n";

    if (existing.size() + 1 > static_cast<size_t>(numeric_limits<int>::max()))
    {
        if (!readText("Enter category (enter 0 to cancel): ", category) || category == "0")
            return false;
        category = normalizeCategory(category);
        return !category.empty();
    }

    int choice;
    if (!readInteger("Category choice (enter 0 to cancel): ", 0,
                     static_cast<int>(existing.size() + 1), choice)
        || choice == 0)
        return false;

    if (choice == static_cast<int>(existing.size() + 1))
    {
        if (!readText("Enter new category name (enter 0 to cancel): ", category) || category == "0")
            return false;
        category = normalizeCategory(category);
        return !category.empty();
    }

    category = existing[static_cast<size_t>(choice - 1)];
    return true;
}

string formatCents(long long cents)
{
    ostringstream oss;
    if (cents < 0)
    {
        oss << '-';
        long long dollars = -(cents / 100);
        long long remainderCents = -(cents % 100);
        oss << dollars << '.' << setfill('0') << setw(2) << remainderCents;
        return oss.str();
    }
    long long dollars = cents / 100;
    long long remainderCents = cents % 100;
    oss << dollars << '.' << setfill('0') << setw(2) << remainderCents;
    return oss.str();
}

// Parses user input in dollars (e.g. "1" = 100 cents, "0.01" = 1 cent, "12.50" = 1250 cents)
// into integer cents to prevent floating-point rounding issues.
bool parseAmount(const string& input, long long& amount)
{
    size_t start = input.find_first_not_of(" \t\r\n");
    if (start == string::npos)
        return false;
    size_t end = input.find_last_not_of(" \t\r\n");
    string trimmed = input.substr(start, end - start + 1);

    bool negative = false;
    size_t i = 0;
    if (trimmed[i] == '-')
    {
        negative = true;
        i++;
    }
    else if (trimmed[i] == '+')
    {
        i++;
    }

    if (i >= trimmed.size())
        return false;

    size_t dotPos = string::npos;
    bool hasDigits = false;
    for (size_t j = i; j < trimmed.size(); ++j)
    {
        if (trimmed[j] == '.')
        {
            if (dotPos != string::npos)
                return false;
            dotPos = j;
        }
        else if (isdigit(static_cast<unsigned char>(trimmed[j])))
        {
            hasDigits = true;
        }
        else
        {
            return false;
        }
    }
    if (!hasDigits)
        return false;

    long long dollars = 0;
    long long centVal = 0;
    if (dotPos == string::npos)
    {
        try {
            dollars = stoll(trimmed.substr(i));
        } catch (...) {
            return false;
        }
        if (dollars > (numeric_limits<long long>::max() / 100) - 1)
            return false;
        centVal = 0;
    }
    else
    {
        string dollarPart = trimmed.substr(i, dotPos - i);
        string centPart = trimmed.substr(dotPos + 1);
        if (!dollarPart.empty())
        {
            try {
                dollars = stoll(dollarPart);
            } catch (...) {
                return false;
            }
        }
        if (dollars > (numeric_limits<long long>::max() / 100) - 1)
            return false;

        if (centPart.empty())
        {
            centVal = 0;
        }
        else if (centPart.size() == 1)
        {
            centVal = (centPart[0] - '0') * 10;
        }
        else if (centPart.size() == 2)
        {
            centVal = (centPart[0] - '0') * 10 + (centPart[1] - '0');
        }
        else
        {
            int c = (centPart[0] - '0') * 10 + (centPart[1] - '0');
            if (centPart[2] >= '5')
                c += 1;
            centVal = c;
        }
    }

    long long totalCents = dollars * 100 + centVal;
    if (negative)
        totalCents = -totalCents;

    amount = totalCents;
    return true;
}

// Parses an amount stored in a file. If it contains a decimal point, it was stored
// as legacy floating-point dollars (e.g. "36.490000000000002" -> 3649 cents).
// If no decimal point, it is read directly as stored integer cents.
bool parseStoredAmount(const string& input, long long& amount)
{
    size_t start = input.find_first_not_of(" \t\r\n");
    if (start == string::npos)
        return false;
    size_t end = input.find_last_not_of(" \t\r\n");
    string trimmed = input.substr(start, end - start + 1);

    if (trimmed.find('.') != string::npos)
    {
        return parseAmount(trimmed, amount);
    }

    try {
        size_t idx = 0;
        long long val = stoll(trimmed, &idx);
        if (idx != trimmed.size())
            return false;
        amount = val;
        return true;
    } catch (...) {
        return false;
    }
}

bool readAmount(long long& amount, bool allowCancel = false)
{
    string input;
    const string prompt = allowCancel
        ? "Enter amount (negative for refunds; type \"cancel\" to cancel this expense): "
        : "Enter amount (use negative for refunds): ";
    while (cout << prompt, getline(cin, input))
    {
        size_t start = input.find_first_not_of(" \t\r\n");
        if (start == string::npos)
            continue;
        if (allowCancel && toLowerStr(input.substr(start,
                input.find_last_not_of(" \t\r\n") - start + 1)) == "cancel")
            return false;
        if (parseAmount(input, amount))
            return true;
        cout << "Enter a finite number, such as 12.50 or -5.00.\n";
    }
    return false;
}

bool addExpense(Expense& expense)
{
    cout << "1. Enter a new expense\n"
         << "2. Repeat a past expense\n"
         << "0. Cancel and return to the main menu\n\n";
    int method;
    if (!readInteger("Choose how to add an expense (enter 0 to cancel): ", 0, 2, method)
        || method == 0)
    {
        clearScreen();
        return false;
    }

    clearScreen();
    Expense draft;
    if (method == 2)
    {
        if (!readRepeatedExpense(draft))
            return false;
    }
    else
    {
        cout << "ADD EXPENSE\n\n";
        if (!readExpenseDate(draft.date))
        {
            clearScreen();
            return false;
        }

        clearScreen();
        cout << "ADD EXPENSE\n\nDate: " << draft.date << "\n\n";
        if (!readCategory(draft.category))
        {
            clearScreen();
            return false;
        }

        clearScreen();
        cout << "ADD EXPENSE\n\nDate: " << draft.date << "\nCategory: " << draft.category << "\n\n";
        if (!readText("Enter description (type \"cancel\" to cancel this expense): ", draft.description)
            || toLowerStr(draft.description) == "cancel")
        {
            clearScreen();
            return false;
        }

        clearScreen();
        cout << "ADD EXPENSE\n\nDate: " << draft.date << "\nCategory: " << draft.category
             << "\nDescription: " << draft.description << "\n\n";
        if (!readAmount(draft.amount, true))
        {
            clearScreen();
            return false;
        }
    }

    // Allocate a fresh ID without changing the source expense or saved history.
    draft.id = getNextExpenseId();
    if (draft.id <= 0)
        return false;
    expense = draft;
    return true;
}

// Stored as integer cents (e.g. 100 for $1.00, 3649 for $36.49).
string expenseToLine(const Expense& expense)
{
    ostringstream line;
    line << expense.id << '|' << expense.date << '|' << expense.category << '|' << expense.description
         << '|' << expense.amount;
    return line.str();
}

bool saveExpense(const Expense& expense)
{
    // Imported or manually edited files may not end with a newline.
    bool needsNewline = false;
    errno = 0;
    ifstream existing(EXPENSE_FILE, ios::binary | ios::ate);
    if (!existing)
    {
        if (errno != ENOENT)
        {
            cout << "Could not inspect the existing file. Nothing was added.\n";
            return false;
        }
        ifstream backup(BACKUP_FILE);
        if (backup)
        {
            cout << "The main file is missing, but a backup exists in " << BACKUP_FILE
                 << ". Restore it before adding expenses.\n";
            return false;
        }
    }
    else
    {
        const streampos size = existing.tellg();
        if (size > streampos(0))
        {
            existing.seekg(-1, ios::end);
            char last = '\0';
            existing.get(last);
            needsNewline = (last != '\n');
        }
        if (size < streampos(0) || !existing)
        {
            cout << "Could not inspect the existing file. Nothing was added.\n";
            return false;
        }
    }
    existing.close();

    ofstream file(EXPENSE_FILE, ios::app);
    if (!file)
    {
        cout << "Could not open " << EXPENSE_FILE << " for writing.\n";
        return false;
    }
    if (needsNewline)
        file << '\n';
    file << expenseToLine(expense) << '\n';
    file.close();
    if (!file)
    {
        cout << "The write failed. Check the file before trying again.\n";
        return false;
    }
    return true;
}

// Validate the record structure without throwing on a damaged amount.
// Supports both new 5-field records (with permanent ID) and legacy 4-field records.
bool splitLine(const string& report, Expense& expense)
{
    istringstream stream(report);
    vector<string> tokens;
    string token;
    while (getline(stream, token, '|'))
    {
        tokens.push_back(token);
    }
    if (!report.empty() && report.back() == '|')
        tokens.push_back("");

    Expense parsed;
    if (tokens.size() == 5)
    {
        istringstream idStream(tokens[0]);
        int parsedId = 0;
        if (!(idStream >> parsedId) || parsedId <= 0)
            return false;
        idStream >> ws;
        if (!idStream.eof())
            return false;

        parsed.id = parsedId;
        parsed.date = tokens[1];
        parsed.category = tokens[2];
        parsed.description = tokens[3];
        int py, pm, pd;
        if (parsed.date.empty() || parsed.category.empty() ||
            !parseDate(parsed.date, py, pm, pd) ||
            !parseStoredAmount(tokens[4], parsed.amount))
            return false;

        expense = parsed;
        return true;
    }
    else if (tokens.size() == 4)
    {
        parsed.id = 0;
        parsed.date = tokens[0];
        parsed.category = tokens[1];
        parsed.description = tokens[2];
        int py, pm, pd;
        if (parsed.date.empty() || parsed.category.empty() ||
            !parseDate(parsed.date, py, pm, pd) ||
            !parseStoredAmount(tokens[3], parsed.amount))
            return false;

        expense = parsed;
        return true;
    }

    return false;
}

// Keep the original text so editing one expense does not reformat other amounts.
// Never silently skip a damaged row before rewriting the file.
bool loadExpenses(vector<Expense>& expenses, vector<string>& originalLines, bool migrateLegacy = true)
{
    expenses.clear();
    originalLines.clear();
    ifstream file(EXPENSE_FILE);
    if (!file)
    {
        cout << "Could not open " << EXPENSE_FILE
             << ". If this is your first use, add an expense first.\n";
        return false;
    }

    string line;
    int lineNum = 0;
    bool hadLegacy = false;
    while (getline(file, line))
    {
        lineNum++;
        if (line.find_first_not_of(" \t\r\n") == string::npos)
            continue;
        Expense expense;
        if (!splitLine(line, expense))
        {
            expenses.clear();
            originalLines.clear();
            cout << "Invalid record on line " << lineNum
                 << ". The operation was stopped; the file was not changed.\n";
            return false;
        }
        if (expense.id == 0 || line.find('.') != string::npos)
            hadLegacy = true;
        expenses.push_back(expense);
        originalLines.push_back(line);
    }
    if (file.bad() || (file.fail() && !file.eof()))
    {
        expenses.clear();
        originalLines.clear();
        cout << "Could not finish reading the file. No changes were made.\n";
        return false;
    }
    file.close();

    if (hadLegacy && migrateLegacy)
    {
        int nextId = 1;
        for (const Expense& e : expenses)
        {
            if (e.id >= nextId)
                nextId = e.id + 1;
        }
        for (size_t i = 0; i < expenses.size(); ++i)
        {
            if (expenses[i].id == 0)
                expenses[i].id = nextId++;
            originalLines[i] = expenseToLine(expenses[i]);
        }
        rewriteExpenses(originalLines);
    }

    return true;
}

// Determines the next permanent unique ID for a newly added expense.
int getNextExpenseId()
{
    vector<Expense> expenses;
    vector<string> originalLines;
    ifstream file(EXPENSE_FILE);
    if (!file)
        return 1;
    file.close();

    if (!loadExpenses(expenses, originalLines, false))
        return -1;

    int maxId = 0;
    for (const Expense& e : expenses)
    {
        if (e.id > maxId)
            maxId = e.id;
    }
    if (maxId == numeric_limits<int>::max())
    {
        cout << "No more expense IDs are available. Nothing was added.\n";
        return -1;
    }
    return maxId + 1;
}

void displayExpense(const Expense& expense, size_t number)
{
    if (number != 0)
    {
        if (expense.id > 0)
            cout << "Expense " << number << " (ID: " << expense.id << ")\n";
        else
            cout << "Expense " << number << '\n';
    }
    else if (expense.id > 0)
    {
        cout << "Expense (ID: " << expense.id << ")\n";
    }
    cout << "Date: " << expense.date << '\n'
         << "Category: " << expense.category << '\n'
         << "Description: " << expense.description << '\n'
         << "Amount: $" << formatCents(expense.amount) << '\n';
}

// Returns today's local date as "YYYY-MM-DD".
string getTodayDate()
{
    time_t now = time(nullptr);
    tm local = {};
#ifdef _WIN32
    if (localtime_s(&local, &now) != 0)
        return "0000-00-00";
#else
    tm* ptr = localtime(&now);
    if (!ptr)
        return "0000-00-00";
    local = *ptr;
#endif
    ostringstream oss;
    oss << setfill('0') << setw(4) << (local.tm_year + 1900) << '-'
        << setw(2) << (local.tm_mon + 1) << '-'
        << setw(2) << local.tm_mday;
    return oss.str();
}

// Extracts year, month, day integers from a "YYYY-MM-DD" string.
bool parseDate(const string& date, int& year, int& month, int& day)
{
    if (date.size() != 10 || date[4] != '-' || date[7] != '-')
        return false;
    for (size_t i = 0; i < 10; ++i)
    {
        if (i == 4 || i == 7) continue;
        if (!isdigit(static_cast<unsigned char>(date[i])))
            return false;
    }
    try {
        year = stoi(date.substr(0, 4));
        month = stoi(date.substr(5, 2));
        day = stoi(date.substr(8, 2));
    } catch (...) {
        return false;
    }
    return (year >= 1 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 && day <= daysInMonth(year, month));
}

// Step back one calendar day, avoiding 24-hour subtraction across DST changes.
bool previousDate(const string& date, string& previous)
{
    int year, month, day;
    if (!parseDate(date, year, month, day) || (year == 1 && month == 1 && day == 1))
        return false;

    if (--day == 0)
    {
        if (--month == 0)
        {
            --year;
            month = 12;
        }
        day = daysInMonth(year, month);
    }

    ostringstream formatted;
    formatted << setfill('0') << setw(4) << year << '-'
              << setw(2) << month << '-' << setw(2) << day;
    previous = formatted.str();
    return true;
}

// Only new expenses use shortcuts; editing and filtering keep manual entry.
bool readExpenseDate(string& date)
{
    const string today = getTodayDate();
    int year, month, day;
    const bool todayAvailable = parseDate(today, year, month, day);
    string yesterday;
    const bool yesterdayAvailable = todayAvailable && previousDate(today, yesterday);

    cout << "Choose an expense date:\n\n"
         << "  1. Today     (" << (todayAvailable ? today : "unavailable") << ")\n"
         << "  2. Yesterday (" << (yesterdayAvailable ? yesterday : "unavailable") << ")\n"
         << "  3. Enter a date manually\n"
         << "  0. Cancel\n\n";

    int choice;
    while (readInteger("Date choice (enter 0 to cancel): ", 0, 3, choice))
    {
        if (choice == 0)
            return false;
        if (choice == 3)
        {
            clearScreen();
            cout << "ADD EXPENSE - MANUAL DATE\n\n";
            return readDate(date);
        }
        if ((choice == 1 && todayAvailable) || (choice == 2 && yesterdayAvailable))
        {
            date = (choice == 1) ? today : yesterday;
            return true;
        }
        cout << "That date is unavailable. Enter 3 for manual date entry or 0 to cancel.\n";
    }
    return false;
}

// Keep the history list compact; the selected expense is shown in full next.
string expensePreviewText(const string& text, size_t limit)
{
    string preview = text.substr(0, limit);
    for (char& ch : preview)
    {
        if (iscntrl(static_cast<unsigned char>(ch)))
            ch = ' ';
    }
    if (text.size() > limit)
        preview += "...";
    return preview;
}

bool readRepeatedExpense(Expense& expense)
{
    vector<Expense> history;
    vector<string> originalLines;
    cout << "REPEAT A PAST EXPENSE\n\n";
    // Browsing and cancellation must not migrate or rewrite existing records.
    if (!loadExpenses(history, originalLines, false))
        return false;
    if (history.empty())
    {
        cout << "There are no saved expenses to repeat. Add a new expense first.\n";
        return false;
    }

    const size_t pageSize = 5;
    size_t pageStart = 0;
    Expense draft;
    bool selected = false;
    while (!selected)
    {
        clearScreen();
        const size_t remaining = history.size() - pageStart;
        const size_t count = remaining < pageSize ? remaining : pageSize;
        const bool hasNextPage = count < remaining;
        cout << "REPEAT A PAST EXPENSE\n\n"
             << "Expenses " << (pageStart + 1) << '-' << (pageStart + count)
             << " of " << history.size() << " (most recently saved first)\n\n";
        for (size_t i = 0; i < count; ++i)
        {
            const Expense& past = history[history.size() - 1 - (pageStart + i)];
            cout << (i + 1) << ". " << past.date << " | "
                 << expensePreviewText(past.category, 24) << " | $" << formatCents(past.amount)
                 << "\n   " << expensePreviewText(past.description, 60) << "\n\n";
        }
        if (hasNextPage)
            cout << "6. Next page\n";
        if (pageStart > 0)
            cout << "7. Previous page\n";
        cout << "0. Cancel and return to the main menu\n\n";

        while (true)
        {
            int choice;
            if (!readInteger("Choose a number shown above (enter 0 to cancel): ", 0, 7, choice)
                || choice == 0)
            {
                clearScreen();
                return false;
            }
            if (choice >= 1 && choice <= static_cast<int>(count))
            {
                draft = history[history.size() - 1 - (pageStart + static_cast<size_t>(choice - 1))];
                // The original ID and date belong only to the original expense.
                draft.id = 0;
                draft.date.clear();
                selected = true;
                break;
            }
            if (choice == 6 && hasNextPage)
            {
                pageStart += pageSize;
                break;
            }
            if (choice == 7 && pageStart > 0)
            {
                pageStart -= pageSize;
                break;
            }
            cout << "That number is not shown on this page. Choose a displayed number or enter 0 to cancel.\n";
        }
    }

    clearScreen();
    cout << "REPEAT A PAST EXPENSE\n\n"
         << "Category: " << draft.category << "\nAmount: $" << formatCents(draft.amount)
         << "\nDescription: " << draft.description << "\n\nChoose a new date for this expense.\n\n";
    if (!readExpenseDate(draft.date))
    {
        clearScreen();
        return false;
    }

    clearScreen();
    cout << "REPEAT A PAST EXPENSE\n\n";
    displayExpense(draft, 0);
    cout << "\n1. Keep this description\n"
         << "2. Edit the description\n"
         << "0. Cancel and return to the main menu\n\n";
    int descriptionChoice;
    if (!readInteger("Description choice (enter 0 to cancel): ", 0, 2, descriptionChoice)
        || descriptionChoice == 0)
    {
        clearScreen();
        return false;
    }
    if (descriptionChoice == 2)
    {
        clearScreen();
        cout << "REPEAT A PAST EXPENSE - EDIT DESCRIPTION\n\n";
        displayExpense(draft, 0);
        cout << '\n';
        if (!readText("Enter new description (type \"cancel\" to cancel this expense): ", draft.description)
            || toLowerStr(draft.description) == "cancel")
        {
            clearScreen();
            return false;
        }
    }

    expense = draft;
    return true;
}

// Convert a civil date to a linear day count for date arithmetic.
int dateToEpochDays(int year, int month, int day)
{
    year -= (month <= 2);
    const int era = (year >= 0 ? year : year - 399) / 400;
    const int yoe = year - era * 400;
    const int doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe;
}

// Returns 0 = Monday, 1 = Tuesday, ... 6 = Sunday.
int dayOfWeek(int year, int month, int day)
{
    tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    mktime(&t);
    return (t.tm_wday + 6) % 7;
}

// True when the expense date falls in the same Mon-Sun week as today.
bool isInCurrentWeek(const string& expenseDate, const string& todayDate)
{
    int ey, em, ed, ty, tmo, td;
    if (!parseDate(expenseDate, ey, em, ed) || !parseDate(todayDate, ty, tmo, td))
        return false;
    int todayDow = dayOfWeek(ty, tmo, td);
    int diff = dateToEpochDays(ey, em, ed) - dateToEpochDays(ty, tmo, td);
    return diff >= -todayDow && diff <= (6 - todayDow);
}

// True when the expense date is in the same calendar month and year as today.
bool isInCurrentMonth(const string& expenseDate, const string& todayDate)
{
    int ey, em, ed, ty, tmo, td;
    if (!parseDate(expenseDate, ey, em, ed) || !parseDate(todayDate, ty, tmo, td))
        return false;
    return ey == ty && em == tmo;
}

// True when the expense date is in the same calendar year as today.
bool isInCurrentYear(const string& expenseDate, const string& todayDate)
{
    int ey, em, ed, ty, tmo, td;
    if (!parseDate(expenseDate, ey, em, ed) || !parseDate(todayDate, ty, tmo, td))
        return false;
    return ey == ty;
}

void viewExpense()
{
    vector<Expense> expenses;
    vector<string> originalLines;
    if (!loadExpenses(expenses, originalLines))
        return;
    if (expenses.empty())
    {
        cout << "There are no saved expenses yet.\n";
        return;
    }
    for (size_t i = 0; i < expenses.size(); ++i)
    {
        displayExpense(expenses[i], i + 1);
        cout << "\n-----------------------------------------------\n\n";
    }
    cout << "End of report.\n";
}

bool totalAmount(long long& total, bool& hasExpenses)
{
    vector<Expense> expenses;
    vector<string> originalLines;
    if (!loadExpenses(expenses, originalLines))
        return false;
    hasExpenses = !expenses.empty();
    total = 0;
    for (const Expense& expense : expenses)
    {
        total += expense.amount;
    }
    return true;
}

// Write the replacement completely before moving the original to a backup.
// rename() cannot overwrite an existing destination on Windows, so move the
// original out of the way first and attempt a rollback if installation fails.
bool rewriteExpenses(const vector<string>& lines)
{
    ifstream existingTemp(TEMP_FILE);
    if (existingTemp)
    {
        existingTemp.close();
        std::remove(TEMP_FILE);
    }

    ofstream temporary(TEMP_FILE, ios::trunc);
    if (!temporary)
    {
        cout << "Could not create the temporary file. The original is unchanged.\n";
        return false;
    }
    for (const string& line : lines)
        temporary << line << '\n';
    temporary.close();
    if (!temporary)
    {
        std::remove(TEMP_FILE);
        cout << "Could not finish writing. The original is unchanged.\n";
        return false;
    }

    if (std::remove(BACKUP_FILE) != 0 && errno != ENOENT)
    {
        std::remove(TEMP_FILE);
        cout << "Could not replace the previous backup. The original is unchanged.\n";
        return false;
    }
    if (std::rename(EXPENSE_FILE, BACKUP_FILE) != 0)
    {
        std::remove(TEMP_FILE);
        cout << "Could not create the backup. The original is unchanged.\n";
        return false;
    }
    if (std::rename(TEMP_FILE, EXPENSE_FILE) != 0)
    {
        if (std::rename(BACKUP_FILE, EXPENSE_FILE) == 0)
        {
            std::remove(TEMP_FILE);
            cout << "Could not save the edit. The original file was restored.\n";
        }
        else
        {
            cout << "Could not restore the original filename. Stop using the program.\n"
                 << "Your original records are in " << BACKUP_FILE
                 << "; the proposed edit is in " << TEMP_FILE << ".\n";
            std::exit(EXIT_FAILURE);
        }
        return false;
    }
    return true;
}

void editExpense()
{
    vector<Expense> expenses;
    vector<string> originalLines;
    if (!loadExpenses(expenses, originalLines))
        return;
    if (expenses.empty())
    {
        cout << "There are no saved expenses to edit.\n";
        return;
    }
    if (expenses.size() > static_cast<size_t>(numeric_limits<int>::max()))
    {
        cout << "There are too many records for this menu.\n";
        return;
    }

    cout << "\nChoose the expense you want to edit:\n";
    for (size_t i = 0; i < expenses.size(); ++i)
    {
        cout << '\n';
        displayExpense(expenses[i], i + 1);
        cout << "-----------------------------------------------\n";
    }

    size_t selectedIndex = 0;
    while (true)
    {
        string prompt = "Expense number (enter 0 to cancel): ";
        string input;
        cout << prompt;
        if (!getline(cin, input))
        {
            clearScreen();
            cout << "Edit cancelled. No changes were saved.\n";
            return;
        }
        size_t pos = input.find_first_not_of(" \t\r\n");
        if (pos == string::npos)
            continue;
        istringstream stream(input);
        int val;
        if (stream >> val)
        {
            stream >> ws;
            if (stream.eof())
            {
                if (val == 0)
                {
                    clearScreen();
                    cout << "Edit cancelled. No changes were saved.\n";
                    return;
                }
                if (val >= 1 && val <= static_cast<int>(expenses.size()))
                {
                    selectedIndex = static_cast<size_t>(val - 1);
                    break;
                }
                bool foundById = false;
                for (size_t i = 0; i < expenses.size(); ++i)
                {
                    if (expenses[i].id == val)
                    {
                        selectedIndex = i;
                        foundById = true;
                        break;
                    }
                }
                if (foundById)
                    break;
            }
        }
        cout << "Enter a whole number from 1 to " << expenses.size() << " (enter 0 to cancel).\n";
    }

    const int targetId = expenses[selectedIndex].id;
    Expense edited = expenses[selectedIndex];
    while (true)
    {
        clearScreen();
        cout << "EDIT EXPENSE (ID: " << targetId << ")\n\nCurrent draft:\n";
        displayExpense(edited, selectedIndex + 1);
        cout << "\n1. Edit date\n2. Edit category\n3. Edit description\n"
             << "4. Edit amount\n5. Save changes\n6. Delete this expense\n0. Cancel all changes\n";

        int field;
        if (!readInteger("Choose a field or action (enter 0 to discard all changes): ", 0, 6, field) || field == 0)
        {
            clearScreen();
            cout << "Edit cancelled. No changes were saved.\n";
            return;
        }
        if (field == 6)
        {
            int origIdx = -1;
            for (size_t i = 0; i < expenses.size(); ++i)
            {
                if (expenses[i].id == targetId)
                {
                    origIdx = static_cast<int>(i);
                    break;
                }
            }
            if (origIdx < 0)
            {
                clearScreen();
                cout << "Error: Expense with ID " << targetId << " was not found.\n";
                return;
            }
            const Expense toDelete = expenses[static_cast<size_t>(origIdx)];
            clearScreen();
            cout << "DELETE EXPENSE (ID: " << targetId << ")\n\n"
                 << "Review the expense to delete:\n";
            displayExpense(toDelete, selectedIndex + 1);
            if (!confirmAction("\nAre you sure you want to delete this expense? (y/n): "))
            {
                clearScreen();
                cout << "Deletion cancelled. Returning to edit draft.\n";
                continue;
            }

            int lineIdxToRemove = -1;
            for (size_t i = 0; i < originalLines.size(); ++i)
            {
                Expense checkExp;
                if (splitLine(originalLines[i], checkExp) && checkExp.id == targetId)
                {
                    lineIdxToRemove = static_cast<int>(i);
                    break;
                }
            }
            if (lineIdxToRemove < 0)
            {
                clearScreen();
                cout << "Error: Could not locate expense ID " << targetId << " in file.\n";
                return;
            }

            DeletedRecord rec;
            rec.expense = toDelete;
            rec.originalIndex = static_cast<size_t>(lineIdxToRemove);

            originalLines.erase(originalLines.begin() + lineIdxToRemove);
            clearScreen();
            if (rewriteExpenses(originalLines))
            {
                g_undoStack.push_back(rec);
                cout << "Expense deleted successfully:\n";
                displayExpense(rec.expense, 0);
                cout << "\nPrevious version saved in " << BACKUP_FILE << ".\n\n";

                if (confirmAction("Undo this deletion now? (y/n): "))
                {
                    clearScreen();
                    performUndo(false);
                }
            }
            return;
        }
        if (field == 5)
        {
            int origIdx = -1;
            for (size_t i = 0; i < expenses.size(); ++i)
            {
                if (expenses[i].id == targetId)
                {
                    origIdx = static_cast<int>(i);
                    break;
                }
            }
            if (origIdx < 0)
            {
                clearScreen();
                cout << "Error: Expense with ID " << targetId << " was not found.\n";
                return;
            }
            const Expense& original = expenses[static_cast<size_t>(origIdx)];
            if (edited.date == original.date && edited.category == original.category &&
                edited.description == original.description && edited.amount == original.amount)
            {
                clearScreen();
                cout << "Nothing changed. The file was not rewritten.\n";
                return;
            }
            clearScreen();
            cout << "Updated expense:\n";
            displayExpense(edited, selectedIndex + 1);
            if (!confirmAction("\nSave these changes? (y/n): "))
            {
                clearScreen();
                cout << "Changes discarded. The file was not changed.\n";
                return;
            }
            clearScreen();
            int lineIdxToUpdate = -1;
            for (size_t i = 0; i < originalLines.size(); ++i)
            {
                Expense checkExp;
                if (splitLine(originalLines[i], checkExp) && checkExp.id == targetId)
                {
                    lineIdxToUpdate = static_cast<int>(i);
                    break;
                }
            }
            if (lineIdxToUpdate < 0)
            {
                cout << "Error: Could not locate expense ID " << targetId << " in file.\n";
                return;
            }
            edited.id = targetId;
            originalLines[static_cast<size_t>(lineIdxToUpdate)] = expenseToLine(edited);
            if (rewriteExpenses(originalLines))
            {
                cout << "Expense updated successfully:\n";
                displayExpense(edited, selectedIndex + 1);
                cout << "Previous version saved in " << BACKUP_FILE << ".\n";
            }
            return;
        }

        bool inputOK = false;
        switch (field)
        {
            case 1:
                clearScreen();
                cout << "EDIT DATE\n\nCurrent date: " << edited.date << "\n\n";
                inputOK = readDate(edited.date);
                break;
            case 2:
                clearScreen();
                cout << "EDIT CATEGORY\n\nCurrent category: " << edited.category << "\n\n";
                inputOK = readCategory(edited.category);
                break;
            case 3:
                clearScreen();
                cout << "EDIT DESCRIPTION\n\nCurrent description: " << edited.description << "\n\n";
                inputOK = readText("New description: ", edited.description);
                break;
            case 4:
                clearScreen();
                cout << "EDIT AMOUNT\n\nCurrent amount: $" << formatCents(edited.amount) << "\n\n";
                inputOK = readAmount(edited.amount);
                break;
        }
        if (!inputOK)
            continue;
    }
}

bool performUndo(bool requireConfirmation)
{
    if (g_undoStack.empty())
    {
        cout << "There are no deleted expenses to undo.\n";
        return false;
    }

    DeletedRecord rec = g_undoStack.back();

    if (requireConfirmation)
    {
        cout << "UNDO DELETION\n\n"
             << "Expense to restore:\n";
        displayExpense(rec.expense, 0);
        cout << '\n';
        if (!confirmAction("Restore this expense? (y/n): "))
        {
            clearScreen();
            cout << "Undo cancelled. The expense was not restored.\n";
            return false;
        }
    }

    vector<Expense> expenses;
    vector<string> originalLines;
    if (!loadExpenses(expenses, originalLines))
    {
        cout << "Could not read expense file to restore record.\n";
        return false;
    }

    bool idTaken = false;
    for (const Expense& e : expenses)
    {
        if (e.id == rec.expense.id)
        {
            idTaken = true;
            break;
        }
    }
    if (idTaken)
    {
        rec.expense.id = getNextExpenseId();
        cout << "Note: Original ID was already in use. Restored with new ID: " << rec.expense.id << ".\n";
    }

    string restoredLine = expenseToLine(rec.expense);
    if (rec.originalIndex <= originalLines.size())
        originalLines.insert(originalLines.begin() + static_cast<ptrdiff_t>(rec.originalIndex), restoredLine);
    else
        originalLines.push_back(restoredLine);

    clearScreen();
    if (rewriteExpenses(originalLines))
    {
        g_undoStack.pop_back();
        cout << "Deletion undone successfully! Restored expense:\n";
        displayExpense(rec.expense, 0);
        cout << "\nPrevious version saved in " << BACKUP_FILE << ".\n";
        return true;
    }
    return false;
}

void deleteExpense()
{
    while (true)
    {
        clearScreen();
        cout << "DELETE EXPENSE\n\n"
             << "1. Delete an expense\n"
             << "2. Undo last deletion\n"
             << "0. Back to main menu\n";

        int option;
        if (!readInteger("Choose an option (enter 0 to return to the main menu): ", 0, 2, option) || option == 0)
        {
            clearScreen();
            return;
        }

        if (option == 2)
        {
            clearScreen();
            performUndo(true);
            cout << "\nPress Enter to return to the Delete Expense menu..." << flush;
            string pauseInput;
            if (!getline(cin, pauseInput))
            {
                clearScreen();
                return;
            }
            continue;
        }

        vector<Expense> expenses;
        vector<string> originalLines;
        if (!loadExpenses(expenses, originalLines))
            return;

        if (expenses.empty())
        {
            clearScreen();
            cout << "There are no saved expenses to delete.\n";
            cout << "\nPress Enter to return to the Delete Expense menu..." << flush;
            string pauseInput;
            if (!getline(cin, pauseInput))
            {
                clearScreen();
                return;
            }
            continue;
        }

        clearScreen();
        cout << "DELETE EXPENSE\n\nChoose the expense you want to delete:\n";
        for (size_t i = 0; i < expenses.size(); ++i)
        {
            cout << '\n';
            displayExpense(expenses[i], i + 1);
            cout << "-----------------------------------------------\n";
        }

        size_t selectedIndex = 0;
        bool haveSelection = false;
        while (true)
        {
            string prompt = "Expense number (enter 0 to cancel): ";
            string input;
            cout << prompt;
            if (!getline(cin, input))
            {
                clearScreen();
                return;
            }
            size_t pos = input.find_first_not_of(" \t\r\n");
            if (pos == string::npos)
                continue;
            istringstream stream(input);
            int val;
            if (stream >> val)
            {
                stream >> ws;
                if (stream.eof())
                {
                    if (val == 0)
                    {
                        clearScreen();
                        break;
                    }
                    if (val >= 1 && val <= static_cast<int>(expenses.size()))
                    {
                        selectedIndex = static_cast<size_t>(val - 1);
                        haveSelection = true;
                        break;
                    }
                    for (size_t i = 0; i < expenses.size(); ++i)
                    {
                        if (expenses[i].id == val)
                        {
                            selectedIndex = i;
                            haveSelection = true;
                            break;
                        }
                    }
                    if (haveSelection)
                        break;
                }
            }
            cout << "Enter a whole number from 1 to " << expenses.size() << " (enter 0 to cancel).\n";
        }

        if (!haveSelection)
            continue;

        const Expense target = expenses[selectedIndex];
        clearScreen();
        cout << "DELETE EXPENSE (ID: " << target.id << ")\n\n"
             << "Review the expense to delete:\n";
        displayExpense(target, selectedIndex + 1);
        cout << '\n';
        if (!confirmAction("Are you sure you want to delete this expense? (y/n): "))
        {
            clearScreen();
            cout << "Deletion cancelled. No changes were made.\n";
            cout << "\nPress Enter to return to the Delete Expense menu..." << flush;
            string pauseInput;
            if (!getline(cin, pauseInput))
            {
                clearScreen();
                return;
            }
            continue;
        }

        int lineIdxToRemove = -1;
        for (size_t i = 0; i < originalLines.size(); ++i)
        {
            Expense checkExp;
            if (splitLine(originalLines[i], checkExp) && checkExp.id == target.id)
            {
                lineIdxToRemove = static_cast<int>(i);
                break;
            }
        }
        if (lineIdxToRemove < 0)
        {
            clearScreen();
            cout << "Error: Could not locate expense ID " << target.id << " in file.\n";
            return;
        }

        DeletedRecord rec;
        rec.expense = target;
        rec.originalIndex = static_cast<size_t>(lineIdxToRemove);

        originalLines.erase(originalLines.begin() + lineIdxToRemove);
        clearScreen();
        if (rewriteExpenses(originalLines))
        {
            g_undoStack.push_back(rec);
            cout << "Expense deleted successfully:\n";
            displayExpense(target, 0);
            cout << "\nPrevious version saved in " << BACKUP_FILE << ".\n\n";

            if (confirmAction("Undo this deletion now? (y/n): "))
            {
                clearScreen();
                performUndo(false);
            }
        }

        cout << "\nPress Enter to return to the Delete Expense menu..." << flush;
        string pauseInput;
        if (!getline(cin, pauseInput))
        {
            clearScreen();
            return;
        }
    }
}

void viewPeriodExpenses()
{
    vector<Expense> expenses;
    vector<string> originalLines;
    if (!loadExpenses(expenses, originalLines))
        return;
    if (expenses.empty())
    {
        cout << "There are no saved expenses yet.\n";
        return;
    }

    string today = getTodayDate();
    int ty, tmo, td;
    if (!parseDate(today, ty, tmo, td))
    {
        cout << "Could not determine today's date.\n";
        return;
    }

    // Compute Monday and Sunday of the current week for the sub-menu display.
    int todayDow = dayOfWeek(ty, tmo, td);
    tm monTm = {};
    monTm.tm_year = ty - 1900;
    monTm.tm_mon  = tmo - 1;
    monTm.tm_mday = td - todayDow;
    mktime(&monTm);
    tm sunTm = {};
    sunTm.tm_year = ty - 1900;
    sunTm.tm_mon  = tmo - 1;
    sunTm.tm_mday = td + (6 - todayDow);
    mktime(&sunTm);

    ostringstream monStr, sunStr;
    monStr << setfill('0') << setw(4) << (monTm.tm_year + 1900) << '-'
           << setw(2) << (monTm.tm_mon + 1) << '-' << setw(2) << monTm.tm_mday;
    sunStr << setfill('0') << setw(4) << (sunTm.tm_year + 1900) << '-'
           << setw(2) << (sunTm.tm_mon + 1) << '-' << setw(2) << sunTm.tm_mday;

    const char* const monthNames[] =
    {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };

    cout << "Today's date: " << today << "\n\n"
         << "1. This Week   (" << monStr.str() << " to " << sunStr.str() << ")\n"
         << "2. This Month  (" << monthNames[tmo - 1] << " " << ty << ")\n"
         << "3. This Year   (" << ty << ")\n"
         << "0. Back to main menu\n";

    int period;
    if (!readInteger("Choose a period (enter 0 to return to the main menu): ", 0, 3, period) || period == 0)
    {
        clearScreen();
        return;
    }

    // Collect matching expenses.
    vector<size_t> matches;
    for (size_t i = 0; i < expenses.size(); ++i)
    {
        bool match = false;
        switch (period)
        {
            case 1: match = isInCurrentWeek(expenses[i].date, today);  break;
            case 2: match = isInCurrentMonth(expenses[i].date, today); break;
            case 3: match = isInCurrentYear(expenses[i].date, today);  break;
        }
        if (match)
            matches.push_back(i);
    }

    string periodLabel;
    switch (period)
    {
        case 1: periodLabel = "This Week (" + monStr.str() + " to " + sunStr.str() + ")"; break;
        case 2: periodLabel = string("This Month (") + monthNames[tmo - 1] + " " + to_string(ty) + ")"; break;
        case 3: periodLabel = "This Year (" + to_string(ty) + ")"; break;
    }

    clearScreen();
    cout << "SPENDING: " << periodLabel << "\n\n";

    if (matches.empty())
    {
        cout << "No expenses found for this period.\n"
             << "Period total: $0.00\n";
        return;
    }

    long long total = 0;
    for (size_t j = 0; j < matches.size(); ++j)
    {
        displayExpense(expenses[matches[j]], j + 1);
        cout << "\n-----------------------------------------------\n\n";
        total += expenses[matches[j]].amount;
    }

    cout << "Period total (including refunds): $" << formatCents(total) << "\n"
         << matches.size() << " expense(s) found.\n";
}

void searchAndFilterExpenses()
{
    vector<Expense> expenses;
    vector<string> originalLines;
    if (!loadExpenses(expenses, originalLines))
        return;
    if (expenses.empty())
    {
        cout << "There are no saved expenses yet.\n";
        return;
    }

    cout << "1. Search by description\n"
         << "2. Filter by category\n"
         << "3. Filter by date range\n"
         << "4. Filter by amount range\n"
         << "5. Search by expense ID\n"
         << "0. Back to main menu\n";

    int filterType;
    if (!readInteger("Choose a filter (enter 0 to return to the main menu): ", 0, 5, filterType) || filterType == 0)
    {
        clearScreen();
        return;
    }

    vector<size_t> matches;
    string filterLabel;

    switch (filterType)
    {
        case 1:
        {
            clearScreen();
            cout << "SEARCH BY DESCRIPTION\n\n";
            string searchTerm;
            if (!readText("Enter search term: ", searchTerm))
            {
                clearScreen();
                return;
            }
            string lowerTerm = toLowerStr(searchTerm);
            for (size_t i = 0; i < expenses.size(); ++i)
            {
                if (toLowerStr(expenses[i].description).find(lowerTerm) != string::npos)
                    matches.push_back(i);
            }
            filterLabel = "Description containing \"" + searchTerm + "\"";
            break;
        }
        case 2:
        {
            vector<string> categories;
            loadExistingCategories(categories);
            if (categories.empty())
            {
                clearScreen();
                cout << "No categories found in the file.\n";
                return;
            }
            clearScreen();
            cout << "FILTER BY CATEGORY\n\nChoose a category to filter by:\n\n";
            for (size_t i = 0; i < categories.size(); ++i)
                cout << "  " << (i + 1) << ". " << categories[i] << "\n";
            cout << "  0. Cancel\n";

            if (categories.size() > static_cast<size_t>(numeric_limits<int>::max()))
            {
                clearScreen();
                cout << "Too many categories for this menu.\n";
                return;
            }

            int catChoice;
            if (!readInteger("Category (enter 0 to cancel): ", 0,
                             static_cast<int>(categories.size()), catChoice)
                || catChoice == 0)
            {
                clearScreen();
                return;
            }

            string selectedCat = categories[static_cast<size_t>(catChoice - 1)];
            for (size_t i = 0; i < expenses.size(); ++i)
            {
                if (normalizeCategory(expenses[i].category) == selectedCat)
                    matches.push_back(i);
            }
            filterLabel = "Category: " + selectedCat;
            break;
        }
        case 3:
        {
            clearScreen();
            cout << "FILTER BY DATE RANGE\n\nEnter the START date:\n";
            string startDate;
            if (!readDate(startDate))
            {
                clearScreen();
                return;
            }
            clearScreen();
            cout << "FILTER BY DATE RANGE\n\nStart date: " << startDate << "\n\nEnter the END date:\n";
            string endDate;
            if (!readDate(endDate))
            {
                clearScreen();
                return;
            }

            int sy, sm, sd, ey, em, ed;
            if (!parseDate(startDate, sy, sm, sd) ||
                !parseDate(endDate, ey, em, ed))
            {
                clearScreen();
                cout << "Could not interpret the dates.\n";
                return;
            }
            int startDays = dateToEpochDays(sy, sm, sd);
            int endDays   = dateToEpochDays(ey, em, ed);
            if (startDays > endDays)
            {
                cout << "The start date is after the end date. Swapping them.\n";
                swap(startDays, endDays);
                swap(startDate, endDate);
            }

            for (size_t i = 0; i < expenses.size(); ++i)
            {
                int xy, xm, xd;
                if (parseDate(expenses[i].date, xy, xm, xd))
                {
                    int expDays = dateToEpochDays(xy, xm, xd);
                    if (expDays >= startDays && expDays <= endDays)
                        matches.push_back(i);
                }
            }
            filterLabel = "Dates " + startDate + " to " + endDate;
            break;
        }
        case 4:
        {
            clearScreen();
            cout << "FILTER BY AMOUNT RANGE\n\n"
                 << "Enter the minimum and maximum amounts for the range.\n\n";
            long long minAmt = 0, maxAmt = 0;
            string input;

            bool gotMin = false;
            while (cout << "Enter minimum amount: ", getline(cin, input))
            {
                if (input.find_first_not_of(" \t\r\n") == string::npos)
                    continue;
                if (parseAmount(input, minAmt))
                {
                    gotMin = true;
                    break;
                }
                cout << "Enter a finite number, such as 0.00 or -5.00.\n";
            }
            if (!gotMin)
            {
                clearScreen();
                return;
            }

            clearScreen();
            cout << "FILTER BY AMOUNT RANGE\n\n"
                 << "Minimum amount: $" << formatCents(minAmt) << "\n\n";

            bool gotMax = false;
            while (cout << "Enter maximum amount: ", getline(cin, input))
            {
                if (input.find_first_not_of(" \t\r\n") == string::npos)
                    continue;
                if (parseAmount(input, maxAmt))
                {
                    gotMax = true;
                    break;
                }
                cout << "Enter a finite number, such as 100.00 or 500.00.\n";
            }
            if (!gotMax)
            {
                clearScreen();
                return;
            }

            if (minAmt > maxAmt)
            {
                cout << "Minimum is greater than maximum. Swapping them.\n";
                swap(minAmt, maxAmt);
            }

            for (size_t i = 0; i < expenses.size(); ++i)
            {
                if (expenses[i].amount >= minAmt && expenses[i].amount <= maxAmt)
                    matches.push_back(i);
            }

            filterLabel = "Amount $" + formatCents(minAmt) + " to $" + formatCents(maxAmt);
            break;
        }
        case 5:
        {
            clearScreen();
            cout << "SEARCH BY EXPENSE ID\n\n";
            int searchId = 0;
            if (!readInteger("Enter expense ID (enter 0 to cancel): ", 0, numeric_limits<int>::max(), searchId) || searchId == 0)
            {
                clearScreen();
                return;
            }
            for (size_t i = 0; i < expenses.size(); ++i)
            {
                if (expenses[i].id == searchId)
                    matches.push_back(i);
            }
            filterLabel = "Expense ID " + to_string(searchId);
            break;
        }
    }

    clearScreen();
    cout << "SEARCH RESULTS: " << filterLabel << "\n\n";

    if (matches.empty())
    {
        cout << "No matching expenses found.\n";
        return;
    }

    long long total = 0;
    for (size_t j = 0; j < matches.size(); ++j)
    {
        displayExpense(expenses[matches[j]], j + 1);
        cout << "\n-----------------------------------------------\n\n";
        total += expenses[matches[j]].amount;
    }

    cout << matches.size() << " expense(s) found.\n"
         << "Matching total (including refunds): $" << formatCents(total) << "\n";
}

bool splitBudgetLine(const string& line, Budget& budget)
{
    istringstream stream(line);
    string amtStr;
    Budget parsed;
    if (!getline(stream, parsed.type, '|') ||
        !getline(stream, parsed.name, '|') ||
        !getline(stream, amtStr))
        return false;

    if (amtStr.find('|') != string::npos)
        return false;

    if (parsed.type == "TIMEFRAME")
    {
        if (parsed.name != "WEEK" && parsed.name != "MONTH" && parsed.name != "YEAR")
            return false;
    }
    else if (parsed.type == "CATEGORY")
    {
        parsed.name = normalizeCategory(parsed.name);
        if (parsed.name.find_first_not_of(" \t\r\n") == string::npos)
            return false;
    }
    else
    {
        return false;
    }

    if (!parseStoredAmount(amtStr, parsed.amount) || parsed.amount <= 0)
        return false;

    budget = parsed;
    return true;
}

// Never silently skip a damaged row before rewriting the budget file.
bool loadBudgets(vector<Budget>& budgets)
{
    budgets.clear();
    ifstream file(BUDGET_FILE);
    if (!file)
        return true;

    string line;
    int lineNum = 0;
    while (getline(file, line))
    {
        lineNum++;
        if (line.find_first_not_of(" \t\r\n") == string::npos)
            continue;
        Budget b;
        if (!splitBudgetLine(line, b))
        {
            budgets.clear();
            cout << "Invalid budget record on line " << lineNum
                 << ". The operation was stopped; the file was not changed.\n";
            return false;
        }
        budgets.push_back(b);
    }

    if (file.bad() || (file.fail() && !file.eof()))
    {
        budgets.clear();
        cout << "Could not finish reading the budget file. No changes were made.\n";
        return false;
    }
    return true;
}

bool saveAllBudgets(const vector<Budget>& budgets)
{
    ifstream existingTemp(BUDGET_TEMP_FILE);
    if (existingTemp)
    {
        existingTemp.close();
        std::remove(BUDGET_TEMP_FILE);
    }

    ofstream temp(BUDGET_TEMP_FILE, ios::trunc);
    if (!temp)
    {
        cout << "Could not create temporary budget file.\n";
        return false;
    }

    for (const Budget& b : budgets)
    {
        temp << b.type << '|' << b.name << '|' << b.amount << '\n';
    }
    temp.close();
    if (!temp)
    {
        std::remove(BUDGET_TEMP_FILE);
        cout << "Could not finish writing budget file.\n";
        return false;
    }

    std::remove(BUDGET_BACKUP_FILE);
    ifstream existingMain(BUDGET_FILE);
    if (existingMain)
    {
        existingMain.close();
        if (std::rename(BUDGET_FILE, BUDGET_BACKUP_FILE) != 0)
        {
            std::remove(BUDGET_TEMP_FILE);
            cout << "Could not create budget backup file.\n";
            return false;
        }
    }

    if (std::rename(BUDGET_TEMP_FILE, BUDGET_FILE) != 0)
    {
        std::rename(BUDGET_BACKUP_FILE, BUDGET_FILE);
        std::remove(BUDGET_TEMP_FILE);
        cout << "Could not save budget file. Rolled back.\n";
        return false;
    }
    return true;
}

long long calculateTimeframeSpent(const vector<Expense>& expenses, const string& timeframe, const string& today)
{
    long long total = 0;
    for (const Expense& e : expenses)
    {
        bool match = false;
        if (timeframe == "WEEK")
            match = isInCurrentWeek(e.date, today);
        else if (timeframe == "MONTH")
            match = isInCurrentMonth(e.date, today);
        else if (timeframe == "YEAR")
            match = isInCurrentYear(e.date, today);

        if (match)
            total += e.amount;
    }
    return total;
}

long long calculateCategorySpent(const vector<Expense>& expenses, const string& categoryName, const string& today = "")
{
    string targetToday = today.empty() ? getTodayDate() : today;
    long long total = 0;
    for (const Expense& e : expenses)
    {
        if (normalizeCategory(e.category) == categoryName && isInCurrentMonth(e.date, targetToday))
            total += e.amount;
    }
    return total;
}

void displayBudgetComparison(long long budgetAmount, long long spentAmount, const string& label)
{
    cout << "Budget:       $" << formatCents(budgetAmount) << "\n"
         << "Amount Spent: $" << formatCents(spentAmount) << "\n";
    if (spentAmount <= budgetAmount)
    {
        long long remaining = budgetAmount - spentAmount;
        cout << "Status:       You still have $" << formatCents(remaining)
             << " remaining for " << label << ".\n";
    }
    else
    {
        long long exceeded = spentAmount - budgetAmount;
        cout << "Status:       ALERT! You have exceeded your budget by $"
             << formatCents(exceeded) << " for " << label << "!\n";
    }
}

bool readBudgetAmount(const string& prompt, long long& amount)
{
    string input;
    while (cout << prompt, getline(cin, input))
    {
        size_t pos = input.find_first_not_of(" \t\r\n");
        if (pos == string::npos)
            continue;
        long long candidate;
        if (parseAmount(input, candidate))
        {
            if (candidate == 0)
            {
                amount = 0;
                return true;
            }
            if (candidate > 0)
            {
                amount = candidate;
                return true;
            }
        }
        cout << "Enter a positive number, such as 50.00 or 200.00 (enter 0 to cancel).\n";
    }
    return false;
}

void viewBudgetStatus(const vector<Expense>& expenses, const vector<Budget>& budgets, const string& today)
{
    clearScreen();
    cout << "BUDGET STATUS\n\n";

    if (budgets.empty())
    {
        cout << "No budgets have been set yet.\n"
             << "Choose option 2 or 3 from the Budget Management menu to set a budget.\n";
        return;
    }

    bool hasTimeframe = false;
    cout << "--- TIMEFRAME BUDGETS ---\n\n";
    const char* const tfKeys[] = {"WEEK", "MONTH", "YEAR"};
    const char* const tfNames[] = {"This Week", "This Month", "This Year"};
    for (int i = 0; i < 3; ++i)
    {
        for (const Budget& b : budgets)
        {
            if (b.type == "TIMEFRAME" && b.name == tfKeys[i])
            {
                hasTimeframe = true;
                cout << "[" << tfNames[i] << "]\n";
                long long spent = calculateTimeframeSpent(expenses, tfKeys[i], today);
                displayBudgetComparison(b.amount, spent, tfNames[i]);
                cout << "-----------------------------------------------\n\n";
                break;
            }
        }
    }
    if (!hasTimeframe)
        cout << "No timeframe budgets set.\n\n";

    bool hasCategory = false;
    cout << "--- CATEGORY BUDGETS ---\n\n";
    for (const Budget& b : budgets)
    {
        if (b.type == "CATEGORY")
        {
            hasCategory = true;
            cout << "[Category: " << b.name << "]\n";
            long long spent = calculateCategorySpent(expenses, b.name, today);
            displayBudgetComparison(b.amount, spent, b.name);
            cout << "-----------------------------------------------\n\n";
        }
    }
    if (!hasCategory)
        cout << "No category budgets set.\n";
}

void setTimeframeBudget(const vector<Expense>& expenses, vector<Budget>& budgets, const string& today)
{
    clearScreen();
    cout << "SET TIMEFRAME BUDGET\n\n"
         << "1. Weekly Budget   (This Week)\n"
         << "2. Monthly Budget  (This Month)\n"
         << "3. Yearly Budget   (This Year)\n"
         << "0. Cancel\n";

    int choice;
    if (!readInteger("Choose a timeframe (enter 0 to cancel): ", 0, 3, choice) || choice == 0)
    {
        clearScreen();
        return;
    }

    string tfKey;
    string tfLabel;
    switch (choice)
    {
        case 1: tfKey = "WEEK";  tfLabel = "This Week";  break;
        case 2: tfKey = "MONTH"; tfLabel = "This Month"; break;
        case 3: tfKey = "YEAR";  tfLabel = "This Year";  break;
    }

    clearScreen();
    cout << "SET BUDGET: " << tfLabel << "\n\n";

    long long currentBudget = 0;
    bool hadBudget = false;
    int budgetIdx = -1;
    for (size_t i = 0; i < budgets.size(); ++i)
    {
        if (budgets[i].type == "TIMEFRAME" && budgets[i].name == tfKey)
        {
            currentBudget = budgets[i].amount;
            hadBudget = true;
            budgetIdx = static_cast<int>(i);
            break;
        }
    }

    long long currentSpent = calculateTimeframeSpent(expenses, tfKey, today);
    if (hadBudget)
        cout << "Current budget:      $" << formatCents(currentBudget) << "\n";
    else
        cout << "Current budget:      None\n";
    cout << "Amount spent so far: $" << formatCents(currentSpent) << "\n\n";

    long long newAmount = 0;
    if (!readBudgetAmount("Enter new budget amount (enter 0 to cancel): $", newAmount) || newAmount == 0)
    {
        clearScreen();
        cout << "Budget update cancelled. No changes were made.\n";
        return;
    }

    clearScreen();
    cout << "Confirm " << tfLabel << " Budget:\n\n"
         << "New Budget Amount:   $" << formatCents(newAmount) << "\n"
         << "Amount spent so far: $" << formatCents(currentSpent) << "\n\n";
    if (!confirmAction("Save this budget? (y/n): "))
    {
        clearScreen();
        cout << "Budget update cancelled. No changes were made.\n";
        return;
    }

    if (hadBudget && budgetIdx >= 0)
        budgets[static_cast<size_t>(budgetIdx)].amount = newAmount;
    else
    {
        Budget b;
        b.type = "TIMEFRAME";
        b.name = tfKey;
        b.amount = newAmount;
        budgets.push_back(b);
    }

    clearScreen();
    if (saveAllBudgets(budgets))
    {
        cout << tfLabel << " budget set to $" << formatCents(newAmount) << " successfully!\n\n";
        displayBudgetComparison(newAmount, currentSpent, tfLabel);
    }
}

void setCategoryBudget(const vector<Expense>& expenses, vector<Budget>& budgets, const string& today)
{
    clearScreen();
    cout << "SET CATEGORY BUDGET\n\n";

    vector<string> categories;
    loadExistingCategories(categories);

    for (const Budget& b : budgets)
    {
        if (b.type == "CATEGORY")
        {
            bool found = false;
            for (const string& c : categories)
            {
                if (c == b.name)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                categories.push_back(b.name);
        }
    }

    cout << "Choose a category to set a budget for:\n\n";
    for (size_t i = 0; i < categories.size(); ++i)
        cout << "  " << (i + 1) << ". " << categories[i] << "\n";
    cout << "  " << (categories.size() + 1) << ". Enter a custom category name\n"
         << "  0. Cancel\n";

    int choice;
    if (!readInteger("Category choice (enter 0 to cancel): ", 0,
                     static_cast<int>(categories.size() + 1), choice)
        || choice == 0)
    {
        clearScreen();
        return;
    }

    string categoryName;
    if (choice == static_cast<int>(categories.size() + 1))
    {
        clearScreen();
        cout << "SET CATEGORY BUDGET\n\n";
        if (!readText("Enter category name (enter 0 to cancel): ", categoryName) || categoryName == "0")
        {
            clearScreen();
            return;
        }
        categoryName = normalizeCategory(categoryName);
        if (categoryName.empty())
        {
            clearScreen();
            return;
        }
    }
    else
    {
        categoryName = categories[static_cast<size_t>(choice - 1)];
    }

    clearScreen();
    cout << "SET BUDGET: " << categoryName << "\n\n";

    long long currentBudget = 0;
    bool hadBudget = false;
    int budgetIdx = -1;
    for (size_t i = 0; i < budgets.size(); ++i)
    {
        if (budgets[i].type == "CATEGORY" && budgets[i].name == categoryName)
        {
            currentBudget = budgets[i].amount;
            hadBudget = true;
            budgetIdx = static_cast<int>(i);
            break;
        }
    }

    long long currentSpent = calculateCategorySpent(expenses, categoryName, today);
    if (hadBudget)
        cout << "Current budget:      $" << formatCents(currentBudget) << "\n";
    else
        cout << "Current budget:      None\n";
    cout << "Spent this month:    $" << formatCents(currentSpent) << "\n\n";

    long long newAmount = 0;
    if (!readBudgetAmount("Enter budget amount for " + categoryName + " (enter 0 to cancel): $", newAmount)
        || newAmount == 0)
    {
        clearScreen();
        cout << "Budget update cancelled. No changes were made.\n";
        return;
    }

    clearScreen();
    cout << "Confirm Category Budget:\n\n"
         << "Category:            " << categoryName << "\n"
         << "New Budget Amount:   $" << formatCents(newAmount) << "\n"
         << "Spent this month:    $" << formatCents(currentSpent) << "\n\n";
    if (!confirmAction("Save this budget? (y/n): "))
    {
        clearScreen();
        cout << "Budget update cancelled. No changes were made.\n";
        return;
    }

    if (hadBudget && budgetIdx >= 0)
        budgets[static_cast<size_t>(budgetIdx)].amount = newAmount;
    else
    {
        Budget b;
        b.type = "CATEGORY";
        b.name = categoryName;
        b.amount = newAmount;
        budgets.push_back(b);
    }

    clearScreen();
    if (saveAllBudgets(budgets))
    {
        cout << "Budget for " << categoryName << " set to $" << formatCents(newAmount) << " successfully!\n\n";
        displayBudgetComparison(newAmount, currentSpent, categoryName);
    }
}

void removeBudget(vector<Budget>& budgets)
{
    clearScreen();
    cout << "REMOVE A BUDGET\n\n";

    if (budgets.empty())
    {
        cout << "No budgets are currently set.\n";
        return;
    }

    cout << "Choose a budget to remove:\n\n";
    for (size_t i = 0; i < budgets.size(); ++i)
    {
        string label;
        if (budgets[i].type == "TIMEFRAME")
        {
            if (budgets[i].name == "WEEK") label = "Weekly Budget (This Week)";
            else if (budgets[i].name == "MONTH") label = "Monthly Budget (This Month)";
            else if (budgets[i].name == "YEAR") label = "Yearly Budget (This Year)";
            else label = budgets[i].name;
        }
        else
        {
            label = "Category: " + budgets[i].name;
        }
        cout << "  " << (i + 1) << ". " << label
             << " ($" << formatCents(budgets[i].amount) << ")\n";
    }
    cout << "  0. Cancel\n";

    int choice;
    if (!readInteger("Choice (enter 0 to cancel): ", 0, static_cast<int>(budgets.size()), choice) || choice == 0)
    {
        clearScreen();
        return;
    }

    size_t idx = static_cast<size_t>(choice - 1);
    string removedLabel;
    if (budgets[idx].type == "TIMEFRAME")
    {
        if (budgets[idx].name == "WEEK") removedLabel = "Weekly Budget (This Week)";
        else if (budgets[idx].name == "MONTH") removedLabel = "Monthly Budget (This Month)";
        else if (budgets[idx].name == "YEAR") removedLabel = "Yearly Budget (This Year)";
        else removedLabel = budgets[idx].name;
    }
    else
    {
        removedLabel = "Category: " + budgets[idx].name;
    }
    long long removedAmt = budgets[idx].amount;

    clearScreen();
    cout << "Remove " << removedLabel << " ($" << formatCents(removedAmt) << ")?\n";
    if (!confirmAction("Are you sure? (y/n): "))
    {
        clearScreen();
        cout << "Budget removal cancelled. No changes were made.\n";
        return;
    }

    budgets.erase(budgets.begin() + static_cast<ptrdiff_t>(idx));
    clearScreen();
    if (saveAllBudgets(budgets))
        cout << "Budget removed successfully.\n";
}

void manageBudgets()
{
    vector<Expense> expenses;
    vector<string> originalLines;
    if (!loadExpenses(expenses, originalLines))
        return;

    vector<Budget> budgets;
    if (!loadBudgets(budgets))
        return;

    string today = getTodayDate();

    while (true)
    {
        clearScreen();
        cout << "BUDGET MANAGEMENT\n\n"
             << "1. View Budget Status\n"
             << "2. Set / Update Timeframe Budget\n"
             << "3. Set / Update Category Budget\n"
             << "4. Remove a Budget\n"
             << "0. Back to main menu\n";

        int option;
        if (!readInteger("Choose an option (enter 0 to return to the main menu): ", 0, 4, option) || option == 0)
        {
            clearScreen();
            return;
        }

        switch (option)
        {
            case 1:
                viewBudgetStatus(expenses, budgets, today);
                break;
            case 2:
                setTimeframeBudget(expenses, budgets, today);
                break;
            case 3:
                setCategoryBudget(expenses, budgets, today);
                break;
            case 4:
                removeBudget(budgets);
                break;
        }

        cout << "\nPress Enter to return to the Budget Management menu..." << flush;
        string pauseInput;
        if (!getline(cin, pauseInput))
        {
            clearScreen();
            return;
        }
    }
}

int main()
{
    cout << fixed << setprecision(2);
    clearScreen();
    cout << "This is Darren's Student Expense Tracker\n\n" << flush;
    cout << "Press Enter to continue..." << flush;
    string startInput;
    if (!getline(cin, startInput))
        return 0;

    while (true)
    {
        clearScreen();
        cout << "DARREN'S STUDENT EXPENSE TRACKER\n\nPlease choose an option\n"
             << "1. Add Expense\n2. View Expenses\n3. View Total Spending\n"
             << "4. Edit Expense\n5. Delete Expense\n"
             << "6. View Spending by Time Period\n7. Search & Filter Expenses\n"
             << "8. Budget Management\n9. Exit\n";
        int option;
        if (!readInteger("Option: ", 1, 9, option))
        {
            cout << "\nInput ended. Goodbye!\n";
            return 0;
        }
        clearScreen();
        switch (option)
        {
            case 1:
            {
                cout << "ADD EXPENSE\n\n";
                Expense expense;
                if (!addExpense(expense))
                {
                    cout << "\nExpense cancelled. Returning to the main menu.\n";
                    break;
                }
                clearScreen();
                cout << "Review your expense:\n";
                displayExpense(expense, 0);
                if (!confirmAction("\nSave this expense? (y/n): "))
                {
                    clearScreen();
                    cout << "Expense discarded. Nothing was saved.\n";
                    break;
                }
                clearScreen();
                if (saveExpense(expense))
                {
                    cout << "Expense added:\n";
                    displayExpense(expense, 0);
                }
                break;
            }
            case 2:
                cout << "SAVED EXPENSES\n\n";
                viewExpense();
                break;
            case 3:
            {
                cout << "TOTAL SPENDING\n\n";
                long long total = 0;
                bool hasExpenses;
                if (totalAmount(total, hasExpenses))
                {
                    if (!hasExpenses)
                        cout << "There is no expense history yet. Add an expense first.\n";
                    cout << "Total spending (including refunds): $" << formatCents(total) << '\n';
                }
                break;
            }
            case 4:
                cout << "EDIT EXPENSE\n";
                editExpense();
                break;
            case 5:
                deleteExpense();
                break;
            case 6:
                cout << "SPENDING BY TIME PERIOD\n\n";
                viewPeriodExpenses();
                break;
            case 7:
                cout << "SEARCH & FILTER EXPENSES\n\n";
                searchAndFilterExpenses();
                break;
            case 8:
                manageBudgets();
                break;
            case 9:
                cout << "You have chosen to exit, hope to see you soon!\n";
                return 0;
        }
        if (!waitForEnter())
        {
            cout << "\nInput ended. Goodbye!\n";
            return 0;
        }
    }
}
