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

using namespace std;

const char* const EXPENSE_FILE = "studentExpenses.txt";
const char* const TEMP_FILE = "studentExpenses.txt.tmp";
const char* const BACKUP_FILE = "studentExpenses.txt.bak";

struct Expense
{
    string date;
    string category;
    string description;
    double amount = 0.0;
};

const char* const BUDGET_FILE = "studentBudgets.txt";
const char* const BUDGET_TEMP_FILE = "studentBudgets.txt.tmp";
const char* const BUDGET_BACKUP_FILE = "studentBudgets.txt.bak";

struct Budget
{
    string type; // "TIMEFRAME" or "CATEGORY"
    string name; // "WEEK", "MONTH", "YEAR", or normalized category name
    double amount = 0.0;
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
        istringstream stream(input);
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
    int year, month, day;
    if (!readInteger("Enter the year: ", 1, 9999, year) ||
        !readInteger("Enter the month: ", 1, 12, month) ||
        !readInteger("Enter the day: ", 1, daysInMonth(year, month), day))
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
        if (input.find_first_not_of(" \t\r\n") != string::npos &&
            input.find('|') == string::npos)
        {
            value = input;
            return true;
        }
        cout << "Enter nonempty text without the | character.\n";
    }
    return false;
}

// Normalize a category string to Title Case so "food", "FOOD", and "Food"
// all become "Food".  Prevents duplicate groups from inconsistent casing.
string normalizeCategory(const string& raw)
{
    string result = raw;
    bool capitalizeNext = true;
    for (size_t i = 0; i < result.size(); ++i)
    {
        unsigned char ch = static_cast<unsigned char>(result[i]);
        if (isspace(ch))
            capitalizeNext = true;
        else if (capitalizeNext)
        {
            result[i] = static_cast<char>(toupper(ch));
            capitalizeNext = false;
        }
        else
            result[i] = static_cast<char>(tolower(ch));
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
        {
            cout << "Please enter y or n.\n";
            continue;
        }
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
// Parses lines directly so it does not depend on splitLine or loadExpenses.
bool loadExistingCategories(vector<string>& categories)
{
    categories.clear();
    ifstream file(EXPENSE_FILE);
    if (!file)
        return false;

    string line;
    while (getline(file, line))
    {
        size_t firstPipe = line.find('|');
        if (firstPipe == string::npos)
            continue;
        size_t secondPipe = line.find('|', firstPipe + 1);
        if (secondPipe == string::npos)
            continue;
        string cat = normalizeCategory(
            line.substr(firstPipe + 1, secondPipe - firstPipe - 1));
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
        if (!readText("Enter category: ", category))
            return false;
        category = normalizeCategory(category);
        return true;
    }

    cout << "Choose an existing category or create a new one:\n\n";
    for (size_t i = 0; i < existing.size(); ++i)
        cout << "  " << (i + 1) << ". " << existing[i] << "\n";
    cout << "  " << (existing.size() + 1) << ". Create a new category\n"
         << "  0. Cancel\n";

    if (existing.size() + 1 > static_cast<size_t>(numeric_limits<int>::max()))
    {
        if (!readText("Enter category: ", category))
            return false;
        category = normalizeCategory(category);
        return true;
    }

    int choice;
    if (!readInteger("Category choice: ", 0,
                     static_cast<int>(existing.size() + 1), choice)
        || choice == 0)
        return false;

    if (choice == static_cast<int>(existing.size() + 1))
    {
        if (!readText("Enter new category name: ", category))
            return false;
        category = normalizeCategory(category);
        return true;
    }

    category = existing[static_cast<size_t>(choice - 1)];
    return true;
}

bool parseAmount(const string& input, double& amount)
{
    istringstream stream(input);
    double candidate;
    if (!(stream >> candidate) || !isfinite(candidate))
        return false;
    stream >> ws;
    if (!stream.eof())
        return false;
    amount = candidate;
    return true;
}

bool readAmount(double& amount)
{
    string input;
    while (cout << "Enter amount (use negative for refunds): ", getline(cin, input))
    {
        if (parseAmount(input, amount))
            return true;
        cout << "Enter a finite number, such as 12.50 or -5.00.\n";
    }
    return false;
}

bool addExpense(Expense& expense)
{
    if (!readDate(expense.date))
        return false;

    clearScreen();
    cout << "ADD EXPENSE\n\nDate: " << expense.date << "\n\n";
    if (!readCategory(expense.category))
        return false;

    clearScreen();
    cout << "ADD EXPENSE\n\nDate: " << expense.date << "\nCategory: " << expense.category << "\n\n";
    if (!readText("Enter description: ", expense.description))
        return false;

    clearScreen();
    cout << "ADD EXPENSE\n\nDate: " << expense.date << "\nCategory: " << expense.category
         << "\nDescription: " << expense.description << "\n\n";
    if (!readAmount(expense.amount))
        return false;

    return true;
}

// Storage precision is separate from the two decimal places used for display.
string expenseToLine(const Expense& expense)
{
    ostringstream line;
    line << expense.date << '|' << expense.category << '|' << expense.description
         << '|' << setprecision(numeric_limits<double>::max_digits10) << expense.amount;
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
// Legacy dates remain readable, so the editor can correct them.
bool splitLine(const string& report, Expense& expense)
{
    istringstream stream(report);
    string amountText;
    Expense parsed;
    if (!getline(stream, parsed.date, '|') ||
        !getline(stream, parsed.category, '|') ||
        !getline(stream, parsed.description, '|') ||
        !getline(stream, amountText) ||
        parsed.date.empty() || parsed.category.empty() ||
        !parseAmount(amountText, parsed.amount))
        return false;
    expense = parsed;
    return true;
}

// Keep the original text so editing one expense does not reformat other amounts.
// Never silently skip a damaged row before rewriting the file.
bool loadExpenses(vector<Expense>& expenses, vector<string>& originalLines)
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
    while (getline(file, line))
    {
        Expense expense;
        if (!splitLine(line, expense))
        {
            cout << "Invalid record on line " << originalLines.size() + 1
                 << ". The operation was stopped; the file was not changed.\n";
            return false;
        }
        expenses.push_back(expense);
        originalLines.push_back(line);
    }
    if (file.bad() || (file.fail() && !file.eof()))
    {
        cout << "Could not finish reading the file. No changes were made.\n";
        return false;
    }
    return true;
}

void displayExpense(const Expense& expense, size_t number)
{
    if (number != 0)
        cout << "Expense " << number << '\n';
    cout << "Date: " << expense.date << '\n'
         << "Category: " << expense.category << '\n'
         << "Description: " << expense.description << '\n'
         << "Amount: $" << fixed << setprecision(2) << expense.amount << '\n';
}

// Returns today's local date as "YYYY-MM-DD".
string getTodayDate()
{
    time_t now = time(nullptr);
    tm* local = localtime(&now);
    if (!local)
        return "0000-00-00";
    ostringstream oss;
    oss << setfill('0') << setw(4) << (local->tm_year + 1900) << '-'
        << setw(2) << (local->tm_mon + 1) << '-'
        << setw(2) << local->tm_mday;
    return oss.str();
}

// Extracts year, month, day integers from a "YYYY-MM-DD" string.
bool parseDate(const string& date, int& year, int& month, int& day)
{
    if (date.size() < 10 || date[4] != '-' || date[7] != '-')
        return false;
    istringstream y(date.substr(0, 4));
    istringstream m(date.substr(5, 2));
    istringstream d(date.substr(8, 2));
    return (y >> year) && (m >> month) && (d >> day);
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

bool totalAmount(double& total, bool& hasExpenses)
{
    vector<Expense> expenses;
    vector<string> originalLines;
    if (!loadExpenses(expenses, originalLines))
        return false;
    hasExpenses = !expenses.empty();
    total = 0.0;
    for (const Expense& expense : expenses)
    {
        total += expense.amount;
        if (!isfinite(total))
        {
            cout << "The total is too large to represent.\n";
            return false;
        }
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
        cout << "An earlier temporary file exists: " << TEMP_FILE
             << ". Check it and move it aside before saving another edit.\n";
        return false;
    }
    existingTemp.close();

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

    int selection;
    if (!readInteger("Expense number (0 cancels): ", 0,
                     static_cast<int>(expenses.size()), selection) || selection == 0)
    {
        clearScreen();
        cout << "Edit cancelled. No changes were saved.\n";
        return;
    }

    // People count from 1; vector positions count from 0.
    const size_t index = static_cast<size_t>(selection - 1);
    Expense edited = expenses[index];
    while (true)
    {
        clearScreen();
        cout << "EDIT EXPENSE\n\nCurrent draft:\n";
        displayExpense(edited, static_cast<size_t>(selection));
        cout << "\n1. Edit date\n2. Edit category\n3. Edit description\n"
             << "4. Edit amount\n5. Save changes\n0. Cancel all changes\n";

        int field;
        if (!readInteger("Choose a field or action: ", 0, 5, field) || field == 0)
        {
            clearScreen();
            cout << "Edit cancelled. No changes were saved.\n";
            return;
        }
        if (field == 5)
        {
            const Expense& original = expenses[index];
            if (edited.date == original.date && edited.category == original.category &&
                edited.description == original.description && edited.amount == original.amount)
            {
                clearScreen();
                cout << "Nothing changed. The file was not rewritten.\n";
                return;
            }
            clearScreen();
            cout << "Updated expense:\n";
            displayExpense(edited, static_cast<size_t>(selection));
            if (!confirmAction("\nSave these changes? (y/n): "))
            {
                clearScreen();
                cout << "Changes discarded. The file was not changed.\n";
                return;
            }
            clearScreen();
            originalLines[index] = expenseToLine(edited);
            if (rewriteExpenses(originalLines))
            {
                cout << "Expense updated successfully:\n";
                displayExpense(edited, static_cast<size_t>(selection));
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
                cout << "EDIT AMOUNT\n\nCurrent amount: $" << fixed << setprecision(2) << edited.amount << "\n\n";
                inputOK = readAmount(edited.amount);
                break;
        }
        if (!inputOK)
            continue;
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
    if (!readInteger("Choose a period: ", 0, 3, period) || period == 0)
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

    double total = 0.0;
    for (size_t j = 0; j < matches.size(); ++j)
    {
        displayExpense(expenses[matches[j]], j + 1);
        cout << "\n-----------------------------------------------\n\n";
        total += expenses[matches[j]].amount;
        if (!isfinite(total))
        {
            cout << "The total is too large to represent.\n";
            return;
        }
    }

    cout << "Period total (including refunds): $" << total << "\n"
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
         << "0. Back to main menu\n";

    int filterType;
    if (!readInteger("Choose a filter: ", 0, 4, filterType) || filterType == 0)
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
            if (!readInteger("Category: ", 0,
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
            double minAmt = 0.0, maxAmt = 0.0;
            string input;

            bool gotMin = false;
            while (cout << "Enter minimum amount: ", getline(cin, input))
            {
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
                 << "Minimum amount: $" << fixed << setprecision(2) << minAmt << "\n\n";

            bool gotMax = false;
            while (cout << "Enter maximum amount: ", getline(cin, input))
            {
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

            ostringstream rangeLabel;
            rangeLabel << fixed << setprecision(2)
                       << "Amount $" << minAmt << " to $" << maxAmt;
            filterLabel = rangeLabel.str();
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

    double total = 0.0;
    for (size_t j = 0; j < matches.size(); ++j)
    {
        displayExpense(expenses[matches[j]], j + 1);
        cout << "\n-----------------------------------------------\n\n";
        total += expenses[matches[j]].amount;
        if (!isfinite(total))
        {
            cout << "The total is too large to represent.\n";
            return;
        }
    }

    cout << matches.size() << " expense(s) found.\n"
         << "Matching total (including refunds): $" << total << "\n";
}

bool loadBudgets(vector<Budget>& budgets)
{
    budgets.clear();
    ifstream file(BUDGET_FILE);
    if (!file)
        return true;

    string line;
    while (getline(file, line))
    {
        if (line.empty())
            continue;
        istringstream stream(line);
        Budget b;
        string amtStr;
        if (getline(stream, b.type, '|') &&
            getline(stream, b.name, '|') &&
            getline(stream, amtStr) &&
            parseAmount(amtStr, b.amount))
        {
            budgets.push_back(b);
        }
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
        temp << b.type << '|' << b.name << '|'
             << setprecision(numeric_limits<double>::max_digits10) << b.amount << '\n';
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

double calculateTimeframeSpent(const vector<Expense>& expenses, const string& timeframe, const string& today)
{
    double total = 0.0;
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

double calculateCategorySpent(const vector<Expense>& expenses, const string& categoryName)
{
    double total = 0.0;
    for (const Expense& e : expenses)
    {
        if (normalizeCategory(e.category) == categoryName)
            total += e.amount;
    }
    return total;
}

void displayBudgetComparison(double budgetAmount, double spentAmount, const string& label)
{
    cout << "Budget:       $" << fixed << setprecision(2) << budgetAmount << "\n"
         << "Amount Spent: $" << spentAmount << "\n";
    if (spentAmount <= budgetAmount)
    {
        double remaining = budgetAmount - spentAmount;
        cout << "Status:       You still have $" << remaining
             << " remaining for " << label << ".\n";
    }
    else
    {
        double exceeded = spentAmount - budgetAmount;
        cout << "Status:       ALERT! You have exceeded your budget by $"
             << exceeded << " for " << label << "!\n";
    }
}

bool readBudgetAmount(const string& prompt, double& amount)
{
    string input;
    while (cout << prompt, getline(cin, input))
    {
        size_t pos = input.find_first_not_of(" \t\r\n");
        if (pos == string::npos)
        {
            cout << "Enter a positive number, such as 50.00 or 200.00 (0 cancels).\n";
            continue;
        }
        double candidate;
        if (parseAmount(input, candidate))
        {
            if (candidate == 0.0)
            {
                amount = 0.0;
                return true;
            }
            if (candidate > 0.0)
            {
                amount = candidate;
                return true;
            }
        }
        cout << "Enter a positive number, such as 50.00 or 200.00 (0 cancels).\n";
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
                double spent = calculateTimeframeSpent(expenses, tfKeys[i], today);
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
            double spent = calculateCategorySpent(expenses, b.name);
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
    if (!readInteger("Choose a timeframe: ", 0, 3, choice) || choice == 0)
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

    double currentBudget = 0.0;
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

    double currentSpent = calculateTimeframeSpent(expenses, tfKey, today);
    if (hadBudget)
        cout << "Current budget:      $" << fixed << setprecision(2) << currentBudget << "\n";
    else
        cout << "Current budget:      None\n";
    cout << "Amount spent so far: $" << fixed << setprecision(2) << currentSpent << "\n\n";

    double newAmount = 0.0;
    if (!readBudgetAmount("Enter new budget amount (0 cancels): $", newAmount) || newAmount == 0.0)
    {
        clearScreen();
        cout << "Budget update cancelled. No changes were made.\n";
        return;
    }

    clearScreen();
    cout << "Confirm " << tfLabel << " Budget:\n\n"
         << "New Budget Amount:   $" << fixed << setprecision(2) << newAmount << "\n"
         << "Amount spent so far: $" << currentSpent << "\n\n";
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
        cout << tfLabel << " budget set to $" << fixed << setprecision(2) << newAmount << " successfully!\n\n";
        displayBudgetComparison(newAmount, currentSpent, tfLabel);
    }
}

void setCategoryBudget(const vector<Expense>& expenses, vector<Budget>& budgets)
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
    if (!readInteger("Category choice: ", 0,
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
        if (!readText("Enter category name: ", categoryName))
        {
            clearScreen();
            return;
        }
        categoryName = normalizeCategory(categoryName);
    }
    else
    {
        categoryName = categories[static_cast<size_t>(choice - 1)];
    }

    clearScreen();
    cout << "SET BUDGET: " << categoryName << "\n\n";

    double currentBudget = 0.0;
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

    double currentSpent = calculateCategorySpent(expenses, categoryName);
    if (hadBudget)
        cout << "Current budget:      $" << fixed << setprecision(2) << currentBudget << "\n";
    else
        cout << "Current budget:      None\n";
    cout << "Total spent so far:  $" << fixed << setprecision(2) << currentSpent << "\n\n";

    double newAmount = 0.0;
    if (!readBudgetAmount("Enter budget amount for " + categoryName + " (0 cancels): $", newAmount)
        || newAmount == 0.0)
    {
        clearScreen();
        cout << "Budget update cancelled. No changes were made.\n";
        return;
    }

    clearScreen();
    cout << "Confirm Category Budget:\n\n"
         << "Category:            " << categoryName << "\n"
         << "New Budget Amount:   $" << fixed << setprecision(2) << newAmount << "\n"
         << "Total spent so far:  $" << currentSpent << "\n\n";
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
        cout << "Budget for " << categoryName << " set to $" << fixed << setprecision(2) << newAmount << " successfully!\n\n";
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
             << " ($" << fixed << setprecision(2) << budgets[i].amount << ")\n";
    }
    cout << "  0. Cancel\n";

    int choice;
    if (!readInteger("Choice: ", 0, static_cast<int>(budgets.size()), choice) || choice == 0)
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
    double removedAmt = budgets[idx].amount;

    clearScreen();
    cout << "Remove " << removedLabel << " ($" << fixed << setprecision(2) << removedAmt << ")?\n";
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
    loadExpenses(expenses, originalLines);

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
        if (!readInteger("Choose an option: ", 0, 4, option) || option == 0)
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
                setCategoryBudget(expenses, budgets);
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
#ifdef _WIN32
    std::system("pause");
#else
    cout << "Press Enter to continue..." << flush;
    string startInput;
    if (!getline(cin, startInput))
        return 0;
#endif

    while (true)
    {
        clearScreen();
        cout << "DARREN'S STUDENT EXPENSE TRACKER\n\nPlease choose an option\n"
             << "1. Add Expense\n2. View Expenses\n3. View Total Spending\n"
             << "4. Edit Expense\n5. View Spending by Time Period\n"
             << "6. Search & Filter Expenses\n7. Budget Management\n8. Exit\n";
        int option;
        if (!readInteger("Option: ", 1, 8, option))
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
                    clearScreen();
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
                double total;
                bool hasExpenses;
                if (totalAmount(total, hasExpenses))
                {
                    if (!hasExpenses)
                        cout << "There is no expense history yet. Add an expense first.\n";
                    cout << "Total spending (including refunds): $" << total << '\n';
                }
                break;
            }
            case 4:
                cout << "EDIT EXPENSE\n";
                editExpense();
                break;
            case 5:
                cout << "SPENDING BY TIME PERIOD\n\n";
                viewPeriodExpenses();
                break;
            case 6:
                cout << "SEARCH & FILTER EXPENSES\n\n";
                searchAndFilterExpenses();
                break;
            case 7:
                manageBudgets();
                break;
            case 8:
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