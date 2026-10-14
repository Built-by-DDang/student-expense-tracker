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
    return readDate(expense.date) &&
           readText("Enter category: ", expense.category) &&
           readText("Enter description: ", expense.description) &&
           readAmount(expense.amount);
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
            case 1: inputOK = readDate(edited.date); break;
            case 2: inputOK = readText("New category: ", edited.category); break;
            case 3: inputOK = readText("New description: ", edited.description); break;
            case 4: inputOK = readAmount(edited.amount); break;
        }
        if (!inputOK)
        {
            cout << "Input ended. No changes were saved.\n";
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
    if (!readInteger("Choose a period: ", 0, 3, period) || period == 0)
        return;

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
             << "4. Edit Expense\n5. View Spending by Time Period\n6. Exit\n";
        int option;
        if (!readInteger("Option: ", 1, 6, option))
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
                    cout << "\nInput ended. The expense was not saved.\n";
                    return 0;
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